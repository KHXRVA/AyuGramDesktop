// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/layers/generic_box.h"

namespace Main {
class Session;
} // namespace Main

namespace AyuUi {

// Edits a list of pinned reactions. Entries are either plain emoji or
// "custom:<document id>" for custom emoji.
void PinnedReactionsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Main::Session*> session,
	rpl::producer<QString> title,
	std::vector<QString> current,
	Fn<void(std::vector<QString>)> save);

} // namespace AyuUi
