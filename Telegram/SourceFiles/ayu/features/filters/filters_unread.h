// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

class History;
class HistoryItem;

namespace FiltersUnread {

[[nodiscard]] bool hidden(not_null<HistoryItem*> item);
void resolve(not_null<History*> history);

}
