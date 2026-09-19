// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "ui/layers/generic_box.h"

namespace Window {
class SessionController;
}

namespace AyuUi {

void PinnedReactionsBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionController*> controller,
	rpl::producer<QString> title,
	std::vector<QString> current,
	Fn<void(std::vector<QString>)> save);

}
