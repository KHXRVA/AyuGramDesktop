// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/deleted_media.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"

#include "base/call_delayed.h"
#include "core/file_location.h"
#include "core/version.h"
#include "settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/serialize_document.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/image/image.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>

#include <atomic>
#include <map>

namespace AyuMessages {
namespace {

constexpr auto kThumbnailSide = 320;

[[nodiscard]] QString MessageFolder(ID userId, ID dialogId) {
	return mediaRootPath()
		+ QString::number(userId)
		+ '/'
		+ QString::number(dialogId)
		+ '/';
}

[[nodiscard]] QString RelativeFromAbsolute(const QString &absolute) {
	const auto root = mediaRootPath();
	return absolute.startsWith(root) ? absolute.mid(root.size()) : absolute;
}

[[nodiscard]] QString AbsoluteFromRelative(const std::string &stored) {
	const auto path = QString::fromStdString(stored);
	if (path.isEmpty() || path == "/") {
		return QString();
	}
	if (QFileInfo(path).isAbsolute()) {
		return path;
	}
	return mediaRootPath() + path;
}

[[nodiscard]] bool WriteBytes(const QString &path, const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return false;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

[[nodiscard]] QByteArray ReadBytes(const QString &path) {
	if (path.isEmpty()) {
		return {};
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

[[nodiscard]] QByteArray EncodeJpeg(const QImage &image, int quality) {
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "JPG", quality);
	buffer.close();
	return bytes;
}

[[nodiscard]] QString ExtensionFor(
		not_null<DocumentData*> document,
		const QString &fallback) {
	const auto name = document->filename();
	const auto dot = name.lastIndexOf('.');
	if (dot > 0 && name.size() - dot <= 6) {
		return name.mid(dot);
	}
	const auto mime = document->mimeString();
	if (document->isVoiceMessage()) {
		return u".ogg"_q;
	} else if (document->isVideoMessage()
		|| document->isVideoFile()
		|| mime == u"video/mp4"_q) {
		return u".mp4"_q;
	} else if (document->isGifv()) {
		return u".mp4"_q;
	} else if (mime == u"image/webp"_q) {
		return u".webp"_q;
	} else if (mime == u"application/x-tgsticker"_q) {
		return u".tgs"_q;
	} else if (mime == u"image/jpeg"_q) {
		return u".jpg"_q;
	} else if (mime == u"image/png"_q) {
		return u".png"_q;
	}
	const auto suffix = QMimeDatabase().mimeTypeForName(mime).preferredSuffix();
	return suffix.isEmpty() ? fallback : ('.' + suffix);
}

void SaveThumbnail(
		const QImage &image,
		const QString &basePath,
		AyuMessageBase &message) {
	if (image.isNull()) {
		return;
	}
	const auto scaled = (image.width() > kThumbnailSide
		|| image.height() > kThumbnailSide)
		? image.scaled(
			kThumbnailSide,
			kThumbnailSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image;
	const auto path = basePath + u"_thumb.jpg"_q;
	if (WriteBytes(path, EncodeJpeg(scaled, 80))) {
		message.hqThumbPath = RelativeFromAbsolute(path).toStdString();
	}
}

[[nodiscard]] std::vector<char> SerializeDocument(
		not_null<DocumentData*> document) {
	auto bytes = QByteArray();
	{
		auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		Serialize::Document::writeToStream(stream, document);
	}
	return std::vector<char>(bytes.begin(), bytes.end());
}

}

QString mediaRootPath() {
	return cWorkingDir() + u"tdata/ayu_media/"_q;
}

namespace {

enum class MediaKind {
	Photo,
	Video,
	Voice,
	Round,
	Gif,
	Sticker,
	File,
};

constexpr auto kMaxSavedFileSize = int64(512 * 1024 * 1024);
constexpr auto kCleanupDelay = crl::time(60 * 1000);
constexpr auto kCleanupMegabyte = int64(1024 * 1024);
constexpr auto kStreamPartSize = int64(128 * 1024);
constexpr auto kStreamInSlice = int64(64) * kStreamPartSize;
constexpr auto kStreamMaxOnlyInHeader = int64(80) * kStreamPartSize;
constexpr auto kMaxAssembledSize = int64(128 * 1024 * 1024);

using StreamParts = std::map<int64, QByteArray>;

[[nodiscard]] MediaKind KindOf(not_null<DocumentData*> document) {
	if (document->isVoiceMessage()) {
		return MediaKind::Voice;
	} else if (document->isVideoMessage()) {
		return MediaKind::Round;
	} else if (document->sticker()) {
		return MediaKind::Sticker;
	} else if (document->isAnimation() || document->isGifv()) {
		return MediaKind::Gif;
	} else if (document->isVideoFile()) {
		return MediaKind::Video;
	}
	return MediaKind::File;
}

[[nodiscard]] bool KindAllowed(MediaKind kind) {
	const auto &settings = AyuSettings::getInstance();
	switch (kind) {
	case MediaKind::Photo: return settings.saveDeletedPhotos();
	case MediaKind::Video: return settings.saveDeletedVideos();
	case MediaKind::Voice: return settings.saveDeletedVoices();
	case MediaKind::Round: return settings.saveDeletedRounds();
	case MediaKind::Gif: return settings.saveDeletedGifs();
	case MediaKind::Sticker: return settings.saveDeletedStickers();
	case MediaKind::File: return settings.saveDeletedFiles();
	}
	return true;
}

[[nodiscard]] QByteArray ParseStreamMap(
		StreamParts &result,
		const QByteArray &data,
		int64 shift,
		int64 maxSize) {
	const auto size = int64(data.size());
	if (!(size % kStreamPartSize) || size == maxSize) {
		if (size > maxSize) {
			return {};
		}
		for (auto offset = int64(); offset < size; offset += kStreamPartSize) {
			result.emplace(
				shift + offset,
				data.mid(offset, std::min(kStreamPartSize, size - offset)));
		}
		return {};
	}
	auto position = int64();
	const auto takeInt = [&]() -> std::optional<uint32> {
		if (position + int64(sizeof(uint32)) > size) {
			return std::nullopt;
		}
		auto value = uint32();
		memcpy(&value, data.constData() + position, sizeof(uint32));
		position += sizeof(uint32);
		return value;
	};
	const auto count = takeInt();
	if (!count) {
		return {};
	}
	if (!*count || *count > uint32(kStreamMaxOnlyInHeader / kStreamPartSize)) {
		return data.mid(position);
	}
	for (auto i = uint32(); i != *count; ++i) {
		const auto offset = int64(takeInt().value_or(0));
		const auto partSize = int64(takeInt().value_or(0));
		if (!partSize
			|| offset >= maxSize
			|| offset + partSize > maxSize
			|| position + partSize > size) {
			return {};
		}
		result.emplace(shift + offset, data.mid(position, partSize));
		position += partSize;
	}
	return data.mid(position);
}

[[nodiscard]] int64 StreamSliceShift(int sliceNumber) {
	return sliceNumber ? (int64(sliceNumber - 1) * kStreamInSlice) : 0;
}

[[nodiscard]] int64 StreamSliceSize(int sliceNumber, int64 size) {
	const auto count = (size + kStreamInSlice - 1) / kStreamInSlice;
	return !sliceNumber
		? size
		: (sliceNumber == count)
		? (size - int64(sliceNumber - 1) * kStreamInSlice)
		: kStreamInSlice;
}

[[nodiscard]] QByteArray AssembleStream(const StreamParts &parts, int64 size) {
	auto result = QByteArray();
	result.reserve(size);
	auto offset = int64();
	while (offset < size) {
		const auto i = parts.find(offset);
		if (i == end(parts) || i->second.isEmpty()) {
			return {};
		}
		result.append(i->second);
		offset += i->second.size();
	}
	if (offset != size) {
		return {};
	}
	return result;
}

struct PendingMedia {
	ID userId = 0;
	ID dialogId = 0;
	ID messageId = 0;
	QString path;
	std::string hqThumbPath;
};

void FinishPending(const PendingMedia &pending, QByteArray bytes) {
	if (bytes.isEmpty() || bytes.startsWith("partial:")) {
		return;
	}
	crl::async([=, bytes = std::move(bytes)] {
		if (!WriteBytes(pending.path, bytes)) {
			return;
		}
		AyuDatabase::updateDeletedMessageMedia(
			pending.userId,
			pending.dialogId,
			pending.messageId,
			RelativeFromAbsolute(pending.path).toStdString(),
			pending.hqThumbPath);
	});
}

void CopyPending(const PendingMedia &pending, const QString &source) {
	crl::async([=] {
		QDir().mkpath(QFileInfo(pending.path).absolutePath());
		if (QFile::exists(pending.path)) {
			QFile::remove(pending.path);
		}
		if (!QFile::copy(source, pending.path)) {
			return;
		}
		AyuDatabase::updateDeletedMessageMedia(
			pending.userId,
			pending.dialogId,
			pending.messageId,
			RelativeFromAbsolute(pending.path).toStdString(),
			pending.hqThumbPath);
	});
}

struct StreamAssembly {
	PendingMedia pending;
	Storage::Cache::Key base;
	int64 size = 0;
	StreamParts parts;
};

void ReadStreamSlice(
		not_null<Main::Session*> session,
		std::shared_ptr<StreamAssembly> state,
		int sliceNumber) {
	const auto key = Storage::Cache::Key{
		state->base.high,
		state->base.low + uint64(sliceNumber),
	};
	session->data().cacheBigFile().get(key, [=](QByteArray &&value) {
		if (!sliceNumber) {
			const auto remaining = ParseStreamMap(
				state->parts,
				value,
				0,
				state->size);
			if (state->size > kStreamMaxOnlyInHeader && !remaining.isEmpty()) {
				[[maybe_unused]] const auto ignore = ParseStreamMap(
					state->parts,
					remaining,
					StreamSliceShift(1),
					StreamSliceSize(1, state->size));
			}
		} else if (!value.isEmpty()) {
			[[maybe_unused]] const auto ignore = ParseStreamMap(
				state->parts,
				value,
				StreamSliceShift(sliceNumber),
				StreamSliceSize(sliceNumber, state->size));
		}
		const auto slices = (state->size <= kStreamMaxOnlyInHeader)
			? 0
			: int((state->size + kStreamInSlice - 1) / kStreamInSlice);
		if (sliceNumber < slices) {
			crl::on_main(session, [=] {
				ReadStreamSlice(session, state, sliceNumber + 1);
			});
			return;
		}
		FinishPending(state->pending, AssembleStream(state->parts, state->size));
	});
}

} // namespace

void saveMediaForMessage(not_null<HistoryItem*> item, AyuMessageBase &message) {
	message.mediaPath = "/";
	message.documentType = int(SavedMediaType::None);
	message.hqThumbPath.clear();
	message.mimeType.clear();
	message.documentSerialized.clear();

	const auto media = item->media();
	if (!media) {
		return;
	}
	scheduleSavedMediaCleanup();
	const auto folder = MessageFolder(message.userId, message.dialogId);
	const auto base = folder + QString::number(message.messageId);

	if (const auto photo = media->photo()) {
		if (!KindAllowed(MediaKind::Photo)) {
			return;
		}
		message.documentType = int(SavedMediaType::Photo);
		message.mimeType = "image/jpeg";
		const auto view = photo->activeMediaView()
			? photo->activeMediaView()
			: photo->createMediaView();
		auto image = QImage();
		for (const auto size : {
				Data::PhotoSize::Large,
				Data::PhotoSize::Thumbnail,
				Data::PhotoSize::Small }) {
			if (const auto loaded = view->image(size)) {
				if (!loaded->isNull()) {
					image = loaded->original();
					break;
				}
			}
		}
		if (image.isNull()) {
			return;
		}
		const auto path = base + u".jpg"_q;
		if (!WriteBytes(path, EncodeJpeg(image, 92))) {
			return;
		}
		message.mediaPath = RelativeFromAbsolute(path).toStdString();
		SaveThumbnail(image, base, message);
		return;
	}

	const auto document = media->document();
	if (!document || !KindAllowed(KindOf(document))) {
		return;
	}
	message.documentType = int(SavedMediaType::Document);
	message.mimeType = document->mimeString().toStdString();
	message.documentSerialized = SerializeDocument(document);

	const auto view = document->activeMediaView()
		? document->activeMediaView()
		: document->createMediaView();
	if (const auto thumb = view->thumbnail()) {
		SaveThumbnail(thumb->original(), base, message);
	}
	const auto bytes = view->bytes();
	if (!bytes.isEmpty()) {
		const auto path = base + ExtensionFor(document, u".bin"_q);
		if (WriteBytes(path, bytes)) {
			message.mediaPath = RelativeFromAbsolute(path).toStdString();
		}
	}
}

void fetchDeletedMedia(
		not_null<HistoryItem*> item,
		const AyuMessageBase &message) {
	if (message.mediaPath != "/"
		|| message.documentType == int(SavedMediaType::None)) {
		return;
	}
	const auto media = item->media();
	if (!media) {
		return;
	}
	scheduleSavedMediaCleanup();
	const auto folder = MessageFolder(message.userId, message.dialogId);
	const auto base = folder + QString::number(message.messageId);
	auto pending = PendingMedia{
		.userId = message.userId,
		.dialogId = message.dialogId,
		.messageId = message.messageId,
		.hqThumbPath = message.hqThumbPath,
	};
	const auto owner = &item->history()->owner();

	if (const auto photo = media->photo()) {
		pending.path = base + u".jpg"_q;
		for (const auto size : {
				Data::PhotoSize::Large,
				Data::PhotoSize::Thumbnail }) {
			const auto key = photo->location(size).file().cacheKey();
			if (key) {
				owner->cache().get(key, [=](QByteArray &&value) {
					FinishPending(pending, std::move(value));
				});
				return;
			}
		}
		return;
	}

	const auto document = media->document();
	if (!document || document->size <= 0 || document->size > kMaxSavedFileSize) {
		return;
	}
	pending.path = base + ExtensionFor(document, u".bin"_q);

	const auto existing = document->filepath(true);
	if (!existing.isEmpty() && QFile::exists(existing)) {
		CopyPending(pending, existing);
		return;
	}
	if (document->saveToCache()) {
		owner->cache().get(document->cacheKey(), [=](QByteArray &&value) {
			FinishPending(pending, std::move(value));
		});
		return;
	}
	if (document->size > kMaxAssembledSize) {
		return;
	}
	const auto state = std::make_shared<StreamAssembly>();
	state->pending = pending;
	state->base = document->bigFileBaseCacheKey();
	state->size = document->size;
	if (!state->base) {
		return;
	}
	ReadStreamSlice(&owner->session(), state, 0);
}

PhotoData *restorePhoto(
		not_null<Main::Session*> session,
		const AyuMessageBase &message) {
	if (message.documentType != int(SavedMediaType::Photo)) {
		return nullptr;
	}
	const auto path = AbsoluteFromRelative(message.mediaPath);
	const auto bytes = ReadBytes(path);
	if (bytes.isEmpty()) {
		return nullptr;
	}
	auto reader = QImageReader(path);
	const auto size = reader.size();
	if (size.isEmpty()) {
		return nullptr;
	}
	const auto thumbBytes = ReadBytes(AbsoluteFromRelative(message.hqThumbPath));
	const auto thumbSize = thumbBytes.isEmpty()
		? size.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio)
		: QImageReader(AbsoluteFromRelative(message.hqThumbPath)).size();

	const auto id = PhotoId(0x7000000000000000ULL
		^ (uint64(message.dialogId) << 20)
		^ uint64(message.messageId));

	const auto large = ImageWithLocation{
		.location = ImageLocation(
			{ InMemoryLocation{ bytes } },
			size.width(),
			size.height()),
		.bytes = bytes,
	};
	const auto thumbnail = thumbBytes.isEmpty()
		? large
		: ImageWithLocation{
			.location = ImageLocation(
				{ InMemoryLocation{ thumbBytes } },
				thumbSize.width(),
				thumbSize.height()),
			.bytes = thumbBytes,
		};
	return session->data().photo(
		id,
		0,
		QByteArray(),
		TimeId(message.date),
		0,
		false,
		QByteArray(),
		thumbnail,
		thumbnail,
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		0);
}

DocumentData *restoreDocument(
		not_null<Main::Session*> session,
		const AyuMessageBase &message) {
	if (message.documentType != int(SavedMediaType::Document)) {
		return nullptr;
	}
	auto document = (DocumentData*)nullptr;
	if (!message.documentSerialized.empty()) {
		auto bytes = QByteArray(
			message.documentSerialized.data(),
			int(message.documentSerialized.size()));
		auto stream = QDataStream(&bytes, QIODevice::ReadOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		document = Serialize::Document::readFromStream(
			session,
			AppVersion,
			stream);
	}
	const auto path = AbsoluteFromRelative(message.mediaPath);
	const auto hasFile = !path.isEmpty() && QFile::exists(path);
	if (!document) {
		if (!hasFile) {
			return nullptr;
		}
		const auto id = DocumentId(0x7100000000000000ULL
			^ (uint64(message.dialogId) << 20)
			^ uint64(message.messageId));
		auto attributes = QVector<MTPDocumentAttribute>();
		attributes.push_back(MTP_documentAttributeFilename(
			MTP_string(QFileInfo(path).fileName())));
		document = session->data().document(
			id,
			0,
			QByteArray(),
			TimeId(message.date),
			attributes,
			QString::fromStdString(message.mimeType),
			InlineImageLocation(),
			ImageWithLocation(),
			ImageWithLocation(),
			false,
			0,
			QFileInfo(path).size());
	}
	if (hasFile) {
		document->setLocation(Core::FileLocation(path));
		if (const auto size = QFileInfo(path).size(); size > 0) {
			document->size = size;
		}
	}
	const auto thumbPath = AbsoluteFromRelative(message.hqThumbPath);
	if (!thumbPath.isEmpty()) {
		const auto thumbBytes = ReadBytes(thumbPath);
		const auto thumbSize = QImageReader(thumbPath).size();
		if (!thumbBytes.isEmpty() && !thumbSize.isEmpty()) {
			document->updateThumbnails(
				InlineImageLocation(),
				ImageWithLocation{
					.location = ImageLocation(
						{ InMemoryLocation{ thumbBytes } },
						thumbSize.width(),
						thumbSize.height()),
					.bytes = thumbBytes,
				},
				ImageWithLocation(),
				false);
		}
	}
	return document;
}

void removeSavedMedia(const AyuMessageBase &message) {
	for (const auto &stored : { message.mediaPath, message.hqThumbPath }) {
		const auto path = AbsoluteFromRelative(stored);
		if (!path.isEmpty() && path.startsWith(mediaRootPath())) {
			QFile::remove(path);
		}
	}
}

void removeSavedMediaForDialog(ID userId, ID dialogId) {
	const auto folder = MessageFolder(userId, dialogId);
	if (folder.startsWith(mediaRootPath())) {
		QDir(folder).removeRecursively();
	}
}

void cleanupSavedMedia() {
	const auto &settings = AyuSettings::getInstance();
	const auto limit = settings.deletedMediaLimit() * kCleanupMegabyte;
	const auto days = settings.deletedMediaDays();
	if (limit <= 0 && days <= 0) {
		return;
	}
	static auto Running = std::atomic<bool>(false);
	if (Running.exchange(true)) {
		return;
	}
	crl::async([=, root = mediaRootPath()] {
		struct Entry {
			QString path;
			int64 size = 0;
			qint64 time = 0;
		};
		auto entries = std::vector<Entry>();
		auto total = int64();
		const auto oldest = (days > 0)
			? QDateTime::currentDateTime().addDays(-days).toMSecsSinceEpoch()
			: qint64();
		auto i = QDirIterator(
			root,
			QDir::Files | QDir::NoSymLinks,
			QDirIterator::Subdirectories);
		while (i.hasNext()) {
			i.next();
			const auto info = i.fileInfo();
			const auto created = info.birthTime();
			const auto time = std::max(
				info.lastModified().toMSecsSinceEpoch(),
				created.isValid() ? created.toMSecsSinceEpoch() : qint64());
			if (oldest > 0 && time < oldest) {
				if (QFile::remove(info.filePath())) {
					continue;
				}
			}
			total += info.size();
			entries.push_back({ info.filePath(), info.size(), time });
		}
		if (limit > 0 && total > limit) {
			std::sort(
				entries.begin(),
				entries.end(),
				[](const Entry &a, const Entry &b) {
					return a.time < b.time;
				});
			const auto target = limit / 10 * 9;
			for (const auto &entry : entries) {
				if (total <= target) {
					break;
				} else if (QFile::remove(entry.path)) {
					total -= entry.size;
				}
			}
		}
		Running.store(false);
	});
}

void scheduleSavedMediaCleanup() {
	static auto Scheduled = false;
	if (std::exchange(Scheduled, true)) {
		return;
	}
	base::call_delayed(kCleanupDelay, [] {
		Scheduled = false;
		cleanupSavedMedia();
	});
}

}
