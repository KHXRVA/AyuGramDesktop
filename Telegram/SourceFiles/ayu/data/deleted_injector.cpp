// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/deleted_injector.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/deleted_media.h"
#include "ayu/data/messages_storage.h"
#include "ayu/utils/ayu_mapper.h"
#include "ayu/utils/telegram_helpers.h"
#include "data/data_channel.h"
#include "data/data_document.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace AyuMessages {
namespace {

constexpr auto kMaxInjectedPerSlice = 200;

// Ids of the items created from the local database that still have to
// be marked as deleted once the history slice is applied.
base::flat_set<FullMsgId> InjectedPending;

[[nodiscard]] MTPPeer PeerToMTP(PeerId id) {
	if (const auto user = peerToUser(id)) {
		return MTP_peerUser(MTP_long(user.bare));
	} else if (const auto chat = peerToChat(id)) {
		return MTP_peerChat(MTP_long(chat.bare));
	} else if (const auto channel = peerToChannel(id)) {
		return MTP_peerChannel(MTP_long(channel.bare));
	}
	return MTP_peerUser(MTP_long(0));
}

[[nodiscard]] PeerId ResolveFrom(
		not_null<History*> history,
		const AyuMessageBase &message) {
	const auto bare = uint64(message.fromId);
	if (!bare) {
		return PeerId(0);
	}
	const auto &owner = history->owner();
	if (owner.userLoaded(UserId(bare))) {
		return peerFromUser(UserId(bare));
	} else if (owner.channelLoaded(ChannelId(bare))) {
		return peerFromChannel(ChannelId(bare));
	} else if (owner.chatLoaded(ChatId(bare))) {
		return peerFromChat(ChatId(bare));
	}
	// Unknown sender, most likely a user we have not seen yet.
	return peerFromUser(UserId(bare));
}

// Rebuilds the document attributes from a DocumentData, so that passing
// the document through Data::Session::processDocument keeps its type.
[[nodiscard]] QVector<MTPDocumentAttribute> AttributesFor(
		not_null<DocumentData*> document) {
	auto result = QVector<MTPDocumentAttribute>();
	const auto dimensions = document->dimensions;
	const auto duration = document->duration();
	if (const auto sticker = document->sticker()) {
		result.push_back(MTP_documentAttributeSticker(
			MTP_flags(0),
			MTP_string(sticker->alt),
			MTP_inputStickerSetEmpty(),
			MTPMaskCoords()));
		if (!dimensions.isEmpty()) {
			result.push_back(MTP_documentAttributeImageSize(
				MTP_int(dimensions.width()),
				MTP_int(dimensions.height())));
		}
		return result;
	}
	if (document->isVoiceMessage()) {
		using Flag = MTPDdocumentAttributeAudio::Flag;
		const auto voice = document->voice();
		const auto waveform = voice
			? documentWaveformEncode5bit(voice->waveform)
			: QByteArray();
		result.push_back(MTP_documentAttributeAudio(
			MTP_flags(Flag::f_voice
				| (waveform.isEmpty() ? Flag(0) : Flag::f_waveform)),
			MTP_int(int(std::max(duration, crl::time(0)) / 1000)),
			MTPstring(),
			MTPstring(),
			MTP_bytes(waveform)));
	} else if (document->isVideoMessage()
		|| document->isVideoFile()
		|| document->isGifv()) {
		using Flag = MTPDdocumentAttributeVideo::Flag;
		const auto w = dimensions.isEmpty() ? 1 : dimensions.width();
		const auto h = dimensions.isEmpty() ? 1 : dimensions.height();
		result.push_back(MTP_documentAttributeVideo(
			MTP_flags(document->isVideoMessage()
				? Flag::f_round_message
				: Flag::f_supports_streaming),
			MTP_double(std::max(duration, crl::time(0)) / 1000.),
			MTP_int(w),
			MTP_int(h),
			MTPint(),
			MTPdouble(),
			MTPstring()));
		if (document->isGifv()) {
			result.push_back(MTP_documentAttributeAnimated());
		}
	} else if (document->isSong()) {
		using Flag = MTPDdocumentAttributeAudio::Flag;
		const auto song = document->song();
		result.push_back(MTP_documentAttributeAudio(
			MTP_flags((song && !song->title.isEmpty() ? Flag::f_title : Flag(0))
				| (song && !song->performer.isEmpty() ? Flag::f_performer : Flag(0))),
			MTP_int(int(std::max(duration, crl::time(0)) / 1000)),
			MTP_string(song ? song->title : QString()),
			MTP_string(song ? song->performer : QString()),
			MTPbytes()));
	} else if (!dimensions.isEmpty()) {
		result.push_back(MTP_documentAttributeImageSize(
			MTP_int(dimensions.width()),
			MTP_int(dimensions.height())));
	}
	if (!document->filename().isEmpty()
		&& !document->isVoiceMessage()
		&& !document->isVideoMessage()) {
		result.push_back(MTP_documentAttributeFilename(
			MTP_string(document->filename())));
	}
	return result;
}

[[nodiscard]] std::optional<MTPMessageMedia> MediaFor(
		not_null<History*> history,
		const AyuMessageBase &message) {
	const auto session = &history->session();
	if (const auto photo = restorePhoto(session, message)) {
		return MTP_messageMediaPhoto(
			MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo),
			MTP_photo(
				MTP_flags(0),
				MTP_long(photo->id),
				MTP_long(0),
				MTP_bytes(),
				MTP_int(message.date),
				MTP_vector<MTPPhotoSize>(),
				MTPVector<MTPVideoSize>(),
				MTP_int(0)),
			MTPint(),
			MTPDocument());
	} else if (const auto document = restoreDocument(session, message)) {
		using Flag = MTPDmessageMediaDocument::Flag;
		auto flags = Flag::f_document;
		if (document->isVoiceMessage()) {
			flags |= Flag::f_voice;
		} else if (document->isVideoMessage()) {
			flags |= Flag::f_round;
		} else if (document->isVideoFile()) {
			flags |= Flag::f_video;
		}
		return MTP_messageMediaDocument(
			MTP_flags(flags),
			MTP_document(
				MTP_flags(0),
				MTP_long(document->id),
				MTP_long(0),
				MTP_bytes(),
				MTP_int(message.date),
				MTP_string(document->mimeString()),
				MTP_long(document->size),
				MTPVector<MTPPhotoSize>(),
				MTPVector<MTPVideoSize>(),
				MTP_int(0),
				MTP_vector<MTPDocumentAttribute>(AttributesFor(document))),
			MTPVector<MTPDocument>(),
			MTPPhoto(),
			MTPint(),
			MTPint());
	}
	return std::nullopt;
}

[[nodiscard]] MTPMessage BuildMessage(
		not_null<History*> history,
		const AyuMessageBase &message) {
	using Flag = MTPDmessage::Flag;
	const auto from = ResolveFrom(history, message);
	const auto self = history->session().userPeerId();
	const auto entities = message.textEntities.empty()
		? MTPVector<MTPMessageEntity>()
		: AyuMapper::deserializeTextWithEntities(message.textEntities);
	const auto media = MediaFor(history, message);
	const auto post = history->peer->isBroadcast();

	auto flags = Flag(0);
	if (from && !post) {
		flags |= Flag::f_from_id;
	}
	if (from == self) {
		flags |= Flag::f_out;
	}
	if (post) {
		flags |= Flag::f_post;
	}
	if (!entities.v.isEmpty()) {
		flags |= Flag::f_entities;
	}
	if (media) {
		flags |= Flag::f_media;
	}
	if (!message.postAuthor.empty()) {
		flags |= Flag::f_post_author;
	}
	if (message.editDate > 0 && message.editDate != message.entityCreateDate) {
		flags |= Flag::f_edit_date;
	}
	if (message.views > 0) {
		flags |= Flag::f_views;
	}
	return MTP_message(
		MTP_flags(flags),
		MTP_int(message.messageId),
		(from && !post) ? PeerToMTP(from) : MTPPeer(),
		MTPint(), // from_boosts_applied
		MTPstring(), // from_rank
		PeerToMTP(history->peer->id),
		MTPPeer(), // saved_peer_id
		MTPMessageFwdHeader(),
		MTPlong(), // via_bot_id
		MTPlong(), // via_business_bot_id
		MTPPeer(), // guestchat_via_from
		MTPMessageReplyHeader(),
		MTP_int(message.date),
		MTP_string(QString::fromStdString(message.text)),
		media ? *media : MTPMessageMedia(),
		MTPReplyMarkup(),
		entities,
		MTP_int(message.views),
		MTPint(), // forwards
		MTPMessageReplies(),
		MTP_int((flags & Flag::f_edit_date) ? message.editDate : 0),
		MTP_string(QString::fromStdString(message.postAuthor)),
		MTPlong(), // grouped_id
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(), // ttl_period
		MTPint(), // quick_reply_shortcut_id
		MTPlong(), // effect
		MTPFactCheck(),
		MTPint(), // report_delivery_until_date
		MTPlong(), // paid_message_stars
		MTPSuggestedPost(),
		MTPint(), // schedule_repeat_period
		MTPstring(), // summary_from_language
		MTPRichMessage());
}

} // namespace

QVector<MTPMessage> mergeDeletedIntoSlice(
		not_null<History*> history,
		const QVector<MTPMessage> &slice,
		MsgId minId,
		MsgId maxId) {
	const auto &settings = AyuSettings::getInstance();
	if (!settings.saveDeletedMessages()
		|| !settings.restoreDeletedInChat()
		|| minId <= 0
		|| (maxId && maxId < minId)) {
		return slice;
	}
	const auto peer = history->peer;
	if (settings.isDeletedSavingExcluded(getDialogIdFromPeer(peer))) {
		return slice;
	}
	// Do not inject into forum topics / monoforums: their slices are
	// filtered by topic and the local storage keys them differently.
	if (peer->isForum() || peer->isMonoforum()) {
		return slice;
	}

	auto present = base::flat_set<MsgId>();
	for (const auto &message : slice) {
		present.emplace(IdFromMessage(message));
	}
	const auto stored = getDeletedMessages(
		peer,
		0,
		ID(minId.bare) - 1,
		maxId ? (ID(maxId.bare) + 1) : ID(0),
		kMaxInjectedPerSlice);
	if (stored.empty()) {
		return slice;
	}

	auto result = slice;
	auto injected = 0;
	for (const auto &message : stored) {
		const auto id = MsgId(message.messageId);
		if (present.contains(id)) {
			continue;
		} else if (history->owner().message(peer->id, id)) {
			continue;
		}
		result.push_back(BuildMessage(history, message));
		InjectedPending.emplace(FullMsgId(peer->id, id));
		++injected;
	}
	if (!injected) {
		return slice;
	}
	ranges::sort(result, ranges::greater(), [](const MTPMessage &message) {
		return IdFromMessage(message).bare;
	});
	return result;
}

void markInjectedAsDeleted(not_null<History*> history) {
	if (InjectedPending.empty()) {
		return;
	}
	auto handled = std::vector<FullMsgId>();
	for (const auto &id : InjectedPending) {
		if (id.peer != history->peer->id) {
			continue;
		}
		if (const auto item = history->owner().message(id)) {
			if (!item->isDeleted()) {
				item->setDeleted();
			}
			handled.push_back(id);
		} else {
			// Item was not created, forget about it.
			handled.push_back(id);
		}
	}
	for (const auto &id : handled) {
		InjectedPending.remove(id);
	}
}

} // namespace AyuMessages
