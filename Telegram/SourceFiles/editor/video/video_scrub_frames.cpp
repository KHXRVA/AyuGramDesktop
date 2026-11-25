/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/video/video_scrub_frames.h"

#include "media/media_video_frames.h"

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

namespace Editor {
namespace {

constexpr auto kStep = crl::time(33);
constexpr auto kMaxSide = 1280;
constexpr auto kMemoryBudget = int64(160) * 1024 * 1024;
constexpr auto kAhead = crl::time(3500);
constexpr auto kBehind = crl::time(500);
constexpr auto kAround = crl::time(2000);
constexpr auto kChunks = 4;
constexpr auto kNearFrames = 4;

[[nodiscard]] QSize FrameBox(QSize box, int count) {
	if (box.width() > kMaxSide || box.height() > kMaxSide) {
		box.scale(kMaxSide, kMaxSide, Qt::KeepAspectRatio);
	}
	const auto bytes = int64(box.width()) * box.height() * 4 * count;
	if (bytes <= kMemoryBudget) {
		return box;
	}
	const auto factor = std::sqrt(kMemoryBudget / float64(bytes));
	return QSize(
		std::max(int(box.width() * factor), 1),
		std::max(int(box.height() * factor), 1));
}

} // namespace

ScrubFrames::ScrubFrames(QString path, QByteArray content)
: _path(std::move(path))
, _content(std::move(content)) {
}

ScrubFrames::~ScrubFrames() {
	cancel();
}

crl::time ScrubFrames::Step() {
	return kStep;
}

void ScrubFrames::cancel() {
	if (const auto flag = base::take(_cancel)) {
		flag->store(true);
	}
}

void ScrubFrames::clear() {
	cancel();
	_frames.clear();
	_from = _till = 0;
}

bool ScrubFrames::covers(crl::time from, crl::time till) const {
	return !_frames.empty() && (from >= _from) && (till <= _till);
}

void ScrubFrames::prepareAround(crl::time position, QSize box) {
	if (!covers(position, position)) {
		prepare(
			std::max(position - kAround, crl::time(0)),
			position + kAround,
			box);
	}
}

void ScrubFrames::follow(crl::time position, QSize box) {
	if (covers(position, position)) {
		return;
	}
	const auto back = !_frames.empty() && (position < _from);
	prepare(
		std::max(position - (back ? kAhead : kBehind), crl::time(0)),
		position + (back ? kBehind : kAhead),
		box);
}

void ScrubFrames::prepare(crl::time from, crl::time till, QSize box) {
	clear();
	if (till <= from || box.isEmpty()) {
		return;
	}
	const auto count = int((till - from + kStep - 1) / kStep) + 1;
	_from = from;
	_till = from + (count - 1) * kStep;
	_frames = std::vector<QImage>(count);
	_cancel = std::make_shared<std::atomic<bool>>(false);

	const auto size = FrameBox(box, count);
	const auto flag = _cancel;
	const auto path = _path;
	const auto content = _content;
	const auto weak = base::make_weak(this);
	const auto chunk = (count + kChunks - 1) / kChunks;
	for (auto first = 0; first < count; first += chunk) {
		auto positions = std::vector<crl::time>();
		for (auto i = first; i != std::min(first + chunk, count); ++i) {
			positions.push_back(from + i * kStep);
		}
		crl::async([=, positions = std::move(positions)] {
			if (flag->load()) {
				return;
			}
			Media::Video::ExtractFrames(path, content, {
				.positions = positions,
				.box = size,
			}, [&](int index, QImage &&frame) {
				if (flag->load()) {
					return false;
				}
				const auto i = first + index;
				crl::on_main(weak, [=, frame = std::move(frame)]() mutable {
					if (!flag->load() && i < int(_frames.size())) {
						_frames[i] = std::move(frame);
					}
				});
				return true;
			});
		});
	}
}

QImage ScrubFrames::find(crl::time position) const {
	if (!covers(position, position)) {
		return QImage();
	}
	const auto count = int(_frames.size());
	const auto index = int(base::SafeRound(
		(position - _from) / float64(kStep)));
	for (auto delta = 0; delta != kNearFrames; ++delta) {
		for (const auto i : { index - delta, index + delta }) {
			if (i >= 0 && i < count && !_frames[i].isNull()) {
				return _frames[i];
			}
		}
	}
	return QImage();
}

} // namespace Editor
