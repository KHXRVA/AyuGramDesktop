// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ui/boxes/pinned_reactions_box.h"

#include "lang_auto.h"
#include "chat_helpers/message_field.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "data/data_user.h"
#include "data/stickers/data_custom_emoji.h"
#include "main/main_session.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_widgets.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"
#include "ui/emoji_config.h"
#include "ui/vertical_list.h"
#include "ui/controls/emoji_button.h"
#include "ui/controls/emoji_button_factory.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

namespace AyuUi {
namespace {

// Builds the field content from the stored entries: plain emoji stay as
// text, custom emoji become custom-emoji tags (rendered as animated ones).
[[nodiscard]] TextWithTags EntriesToText(const std::vector<QString> &entries) {
	auto result = TextWithTags();
	for (const auto &entry : entries) {
		if (!result.text.isEmpty()) {
			result.text.append(' ');
		}
		if (entry.startsWith(u"custom:"_q)) {
			const auto id = entry.mid(7).toULongLong();
			if (!id) {
				continue;
			}
			// Custom emoji need some text to attach the tag to.
			const auto placeholder = QString::fromUtf8("\xF0\x9F\x99\x82");
			result.tags.push_back({
				.offset = int(result.text.size()),
				.length = int(placeholder.size()),
				.id = Ui::InputField::CustomEmojiLink(
					Data::SerializeCustomEmojiId(DocumentId(id))),
			});
			result.text.append(placeholder);
		} else {
			result.text.append(entry);
		}
	}
	return result;
}

[[nodiscard]] std::vector<QString> TextToEntries(const TextWithTags &text) {
	auto result = std::vector<QString>();
	auto covered = std::vector<std::pair<int, int>>();
	for (const auto &tag : text.tags) {
		if (!Ui::InputField::IsCustomEmojiLink(tag.id)) {
			continue;
		}
		const auto data = Ui::InputField::CustomEmojiEntityData(tag.id);
		const auto id = Data::ParseCustomEmojiData(data);
		if (!id) {
			continue;
		}
		covered.push_back({ tag.offset, tag.offset + tag.length });
		const auto entry = u"custom:"_q + QString::number(id);
		if (!ranges::contains(result, entry)) {
			result.push_back(entry);
		}
	}
	// Plain emoji from the rest of the text, keeping the typed order.
	auto entriesInOrder = std::vector<std::pair<int, QString>>();
	for (auto i = 0; i < int(result.size()); ++i) {
		entriesInOrder.push_back({ covered[i].first, result[i] });
	}
	const auto &plain = text.text;
	const auto begin = plain.constData();
	const auto end = begin + plain.size();
	for (auto ch = begin; ch != end;) {
		const auto offset = int(ch - begin);
		const auto inTag = ranges::any_of(covered, [&](const auto &range) {
			return offset >= range.first && offset < range.second;
		});
		if (inTag) {
			++ch;
			continue;
		}
		auto length = 0;
		if (const auto emoji = Ui::Emoji::Find(ch, end, &length)) {
			const auto entry = emoji->text();
			if (!ranges::contains(entriesInOrder, entry, &std::pair<int, QString>::second)) {
				entriesInOrder.push_back({ offset, entry });
			}
			ch += std::max(length, 1);
		} else {
			++ch;
		}
	}
	ranges::sort(entriesInOrder, ranges::less(), &std::pair<int, QString>::first);
	result.clear();
	for (auto &[offset, entry] : entriesInOrder) {
		result.push_back(std::move(entry));
	}
	return result;
}

} // namespace

void PinnedReactionsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		rpl::producer<QString> title,
		std::vector<QString> current,
		Fn<void(std::vector<QString>)> save) {
	const auto session = &controller->session();
	box->setTitle(std::move(title));
	box->setWidth(st::boxWideWidth);

	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::ayu_PinnedReactionsEditTitle()),
		st::boxRowPadding);
	InitMessageFieldHandlers({
		.session = session,
		.field = field,
	});
	field->setTextWithTags(EntriesToText(current));
	field->setMaxLength(512);

	// Emoji panel (regular + custom emoji) right in the box.
	struct State {
		base::unique_qptr<ChatHelpers::TabbedPanel> emojiPanel;
	};
	const auto state = box->lifetime().make_state<State>();
	using Selector = ChatHelpers::TabbedSelector;
	state->emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		box->getDelegate()->outerContainer(),
		controller,
		object_ptr<Selector>(
			nullptr,
			controller->uiShow(),
			Window::GifPauseReason::Layer,
			Selector::Mode::EmojiOnly));
	state->emojiPanel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	state->emojiPanel->hide();
	state->emojiPanel->selector()->setCurrentPeer(session->user());
	state->emojiPanel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(field->textCursor(), data.emoji);
	}, field->lifetime());
	state->emojiPanel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(field, data.document);
	}, field->lifetime());
	const auto emojiButton = Ui::AddEmojiToggleToField(
		field,
		box,
		controller,
		state->emojiPanel.get(),
		st::windowFilterNameEmojiPosition);
	emojiButton->show();

	Ui::AddSkip(box->verticalLayout());
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::ayu_SettingsPinnedReactionsHint(),
			st::boxDividerLabel),
		st::boxRowPadding);

	box->setFocusCallback([=] { field->setFocus(); });
	box->addButton(tr::lng_settings_save(), [=] {
		save(TextToEntries(field->getTextWithTags()));
		box->closeBox();
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace AyuUi
