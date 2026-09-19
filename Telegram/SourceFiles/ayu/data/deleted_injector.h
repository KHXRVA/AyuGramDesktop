// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class History;

namespace AyuMessages {

//
[[nodiscard]] QVector<MTPMessage> mergeDeletedIntoSlice(
	not_null<History*> history,
	const QVector<MTPMessage> &slice,
	MsgId minId,
	MsgId maxId);

void markInjectedAsDeleted(not_null<History*> history);

}
