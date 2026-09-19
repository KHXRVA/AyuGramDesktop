// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class History;

namespace AyuMessages {

// Merges deleted messages saved in the local database into a history
// slice received from the server, so that they keep their place in the
// chat after a restart. Returns the merged slice (sorted newest first).
//
// `minId` / `maxId` describe the id range the slice is responsible for,
// `maxId == 0` means "up to the newest message".
[[nodiscard]] QVector<MTPMessage> mergeDeletedIntoSlice(
	not_null<History*> history,
	const QVector<MTPMessage> &slice,
	MsgId minId,
	MsgId maxId);

// Marks freshly created items from the merged slice as deleted.
void markInjectedAsDeleted(not_null<History*> history);

} // namespace AyuMessages
