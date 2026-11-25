/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <atomic>

namespace Editor {

class ScrubFrames final : public base::has_weak_ptr {
public:
	ScrubFrames(QString path, QByteArray content);
	~ScrubFrames();

	void prepare(crl::time from, crl::time till, QSize box);
	void prepareAround(crl::time position, QSize box);
	void follow(crl::time position, QSize box);
	void clear();

	[[nodiscard]] bool covers(crl::time from, crl::time till) const;
	[[nodiscard]] QImage find(crl::time position) const;
	[[nodiscard]] static crl::time Step();

private:
	void cancel();

	const QString _path;
	const QByteArray _content;

	std::vector<QImage> _frames;
	std::shared_ptr<std::atomic<bool>> _cancel;
	crl::time _from = 0;
	crl::time _till = 0;

};

} // namespace Editor
