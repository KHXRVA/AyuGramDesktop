// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ayu/data/entities.h"

class HistoryItem;
class History;
class PhotoData;
class DocumentData;

namespace Main {
class Session;
} // namespace Main

namespace AyuMessages {

// Media that was saved to disk for a deleted message.
enum class SavedMediaType {
	None = 0,
	Photo = 1,
	Document = 2,
};

// Copies the media of a message (if it is already available locally,
// no downloads are triggered) into tdata/ayu_media and fills the
// media related fields of the entity.
void saveMediaForMessage(not_null<HistoryItem*> item, AyuMessageBase &message);

// Restores a photo / document from a saved entity so that a fake
// message with real media can be shown in the deleted messages viewer.
[[nodiscard]] PhotoData *restorePhoto(
	not_null<Main::Session*> session,
	const AyuMessageBase &message);
[[nodiscard]] DocumentData *restoreDocument(
	not_null<Main::Session*> session,
	const AyuMessageBase &message);

// Removes saved media files for a message (or a whole dialog).
void removeSavedMedia(const AyuMessageBase &message);
void removeSavedMediaForDialog(ID userId, ID dialogId);

[[nodiscard]] QString mediaRootPath();

} // namespace AyuMessages
