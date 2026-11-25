// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/filters/filters_unread.h"

#include "ayu/ayu_settings.h"
#include "ayu/utils/telegram_helpers.h"

#include "base/timer.h"
#include "data/data_histories.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "mtproto/sender.h"

#include <deque>

namespace FiltersUnread {
namespace {

constexpr auto kMaxResolve = 100;
constexpr auto kResolveDelay = crl::time(1500);

struct State {
	explicit State(not_null<Main::Session*> session);

	void next();
	void request(not_null<History*> history);

	const not_null<Main::Session*> session;
	MTP::Sender api;
	base::Timer timer;
	std::deque<PeerId> queue;
	base::flat_map<PeerId, MsgId> resolved;
	mtpRequestId requestId = 0;

};

State::State(not_null<Main::Session*> session)
: session(session)
, api(&session->mtp())
, timer([=] { next(); }) {
}

void State::next() {
	if (requestId) {
		return;
	}
	while (!queue.empty()) {
		const auto peerId = queue.front();
		queue.pop_front();
		const auto history = session->data().historyLoaded(peerId);
		if (history
			&& history->unreadCount() > 1
			&& history->unreadCount() <= kMaxResolve) {
			request(history);
			return;
		}
	}
}

void State::request(not_null<History*> history) {
	const auto peerId = history->peer->id;
	const auto finish = [=] {
		requestId = 0;
		timer.callOnce(kResolveDelay);
	};
	requestId = api.request(MTPmessages_GetHistory(
		history->peer->input(),
		MTP_int(0),
		MTP_int(0),
		MTP_int(0),
		MTP_int(history->unreadCount()),
		MTP_int(0),
		MTP_int(int(history->inboxReadTillId().bare)),
		MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		const auto owner = &session->data();
		result.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			owner->processUsers(data.vusers());
			owner->processChats(data.vchats());
			for (const auto &message : data.vmessages().v) {
				owner->addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);
			}
		});
		if (const auto history = owner->historyLoaded(peerId)) {
			history->refreshHiddenUnread();
		}
		finish();
	}).fail([=](const MTP::Error &) {
		finish();
	}).send();
}

[[nodiscard]] State &StateFor(not_null<Main::Session*> session) {
	static auto states = base::flat_map<
		not_null<Main::Session*>,
		std::unique_ptr<State>>();
	const auto i = states.find(session);
	if (i != end(states)) {
		return *i->second;
	}
	session->lifetime().add([=] {
		states.remove(session);
	});
	return *states.emplace(
		session,
		std::make_unique<State>(session)).first->second;
}

}

bool hidden(not_null<HistoryItem*> item) {
	return AyuSettings::getInstance().hideFilteredFromCounter()
		&& item->isRegular()
		&& !item->out()
		&& isMessageHidden(item);
}

void resolve(not_null<History*> history) {
	const auto count = history->unreadCount();
	if (count < 2 || count > kMaxResolve || history->isForum()) {
		return;
	}
	auto &state = StateFor(&history->session());
	const auto peerId = history->peer->id;
	const auto till = history->inboxReadTillId();
	if (const auto i = state.resolved.find(peerId); i != end(state.resolved)) {
		if (i->second == till) {
			return;
		}
	}
	state.resolved[peerId] = till;
	state.queue.push_back(peerId);
	if (!state.requestId && !state.timer.isActive()) {
		state.timer.callOnce(kResolveDelay);
	}
}

}
