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
}

namespace AyuMessages {

enum class SavedMediaType {
	None = 0,
	Photo = 1,
	Document = 2,
};

void saveMediaForMessage(not_null<HistoryItem*> item, AyuMessageBase &message);

[[nodiscard]] PhotoData *restorePhoto(
	not_null<Main::Session*> session,
	const AyuMessageBase &message);
[[nodiscard]] DocumentData *restoreDocument(
	not_null<Main::Session*> session,
	const AyuMessageBase &message);

void removeSavedMedia(const AyuMessageBase &message);
void removeSavedMediaForDialog(ID userId, ID dialogId);

[[nodiscard]] QString mediaRootPath();

}
