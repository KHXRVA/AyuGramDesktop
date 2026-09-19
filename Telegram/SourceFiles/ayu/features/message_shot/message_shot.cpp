// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/message_shot/message_shot.h"

#include "qguiapplication.h"
#include "ayu/ayu_settings.h"
#include "ayu/ui/boxes/message_shot_box.h"
#include "ayu/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_forum.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "dialogs/ui/dialogs_video_userpic.h"
#include "ui/empty_userpic.h"
#include "ui/image/image_prepare.h"
#include "history/history.h"
#include "history/history_inner_widget.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media.h"
#include "main/main_session.h"
#include "styles/style_ayu_styles.h"
#include "styles/style_chat.h"
#include "styles/style_layers.h"
#include "ui/painter.h"
#include "ui/chat/chat_theme.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/layers/box_content.h"
#include "window/themes/window_theme.h"

namespace AyuFeatures::MessageShot {

ShotConfig *config = nullptr;

bool takingShot = false;
bool choosingTheme = false;

void setShotConfig(ShotConfig &config) {
	MessageShot::config = &config;
}

void resetShotConfig() {
	config = nullptr;
}

ShotConfig getShotConfig() {
	return *config;
}

bool ignoreRender(RenderPart part) {
	if (!config) {
		return false;
	}

	const auto &s = AyuSettings::getInstance().messageShotSettings();
	return isTakingShot()
		&& ((part == RenderPart::Date && !s.showDate())
			|| (part == RenderPart::Reactions && !s.showReactions())
			|| (part == RenderPart::HeaderDecorations
				&& !s.showHeaderDecorations()));
}

bool isTakingShot() {
	return takingShot;
}

bool setChoosingTheme(bool val) {
	choosingTheme = val;
	return choosingTheme;
}

bool isChoosingTheme() {
	return choosingTheme;
}

class MessageShotDelegate final : public HistoryView::DefaultElementDelegate
{
public:
	MessageShotDelegate(
		not_null<QWidget*> parent,
		not_null<Ui::ChatStyle*> st,
		Fn<void()> update,
		not_null<History*> history);

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	HistoryView::Context elementContext() override;
	bool elementHideReply(not_null<const HistoryView::Element*> view) override;
	HistoryView::ElementChatMode elementChatMode() override;

private:
	const not_null<QWidget*> _parent;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;
	not_null<History*> _history;
};

MessageShotDelegate::MessageShotDelegate(
	not_null<QWidget*> parent,
	not_null<Ui::ChatStyle*> st,
	Fn<void()> update,
	not_null<History*> history)
	: _parent(parent)
	  , _pathGradient(HistoryView::MakePathShiftGradient(st, update))
	  , _history(history) {
}

bool MessageShotDelegate::elementAnimationsPaused() {
	return _parent->window()->isActiveWindow();
}

auto MessageShotDelegate::elementPathShiftGradient()
	-> not_null<Ui::PathShiftGradient*> {
	return _pathGradient.get();
}

HistoryView::Context MessageShotDelegate::elementContext() {
	return HistoryView::Context::AdminLog;
}

bool MessageShotDelegate::elementHideReply(not_null<const HistoryView::Element*> view) {
	if (const auto reply = view->data()->Get<HistoryMessageReply>()) {
		const auto replyToPeerId = reply->externalPeerId()
									   ? reply->externalPeerId()
									   : _history->peer->id;

		if (reply->fields().manualQuote) {
			return false;
		} else if (replyToPeerId == _history->peer->id) {
			return _history->asForum() && _history->asForum()->topicFor(reply->messageId());
		}
	}
	return false;
}

HistoryView::ElementChatMode MessageShotDelegate::elementChatMode() {
	using Mode = HistoryView::ElementChatMode;
	return Mode::Wide;
}

QImage removeEmptySpaceAround(const QImage &original) {
	if (original.isNull()) {
		return {};
	}

	int minX = original.width();
	int minY = original.height();
	int maxX = 0;
	int maxY = 0;

	for (int x = 0; x < original.width(); ++x) {
		for (int y = 0; y < original.height(); ++y) {
			if (qAlpha(original.pixel(x, y)) != 0) {
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
		}
	}

	if (minX > maxX || minY > maxY) {
		LOG(("Image is fully transparent ?"));
		return {};
	}

	const QRect bounds(minX, minY, maxX - minX + 1, maxY - minY + 1);
	return original.copy(bounds);
}

QImage addPadding(const QImage &original) {
	if (original.isNull()) {
		return {};
	}

	QImage paddedImage(
		original.width() + 2 * st::messageShotPadding * style::DevicePixelRatio(),
		original.height() + 2 * st::messageShotPadding * style::DevicePixelRatio(),
		QImage::Format_ARGB32_Premultiplied
	);
	paddedImage.setDevicePixelRatio(style::DevicePixelRatio());
	paddedImage.fill(Qt::transparent);

	Painter painter(&paddedImage);
	painter.drawImage(st::messageShotPadding, st::messageShotPadding, original);
	painter.end();

	return paddedImage;
}

QColor makeDefaultBackgroundColor() {
	if (Window::Theme::IsNightMode()) {
		return st::boxBg->c.lighter(175);
	}

	return st::boxBg->c.darker(110);
}


QString DisplayNameFor(not_null<PeerData*> peer) {
	if (isTakingShot()
		&& AyuSettings::getInstance().messageShotSettings().useUsernames()) {
		const auto username = peer->username();
		if (!username.isEmpty()) {
			return username;
		}
	}
	return peer->name();
}

bool ShouldBlurNames() {
	return isTakingShot()
		&& AyuSettings::getInstance().messageShotSettings().blurNames();
}

QImage BlurImage(QImage image, int radius) {
	if (image.isNull()) {
		return image;
	}
	if (image.format() != QImage::Format_ARGB32_Premultiplied) {
		image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	}
	return Images::BlurLargeImage(std::move(image), radius);
}

void PaintBlurredBlock(
		QPainter &p,
		const QRect &rect,
		const QColor &color) {
	if (rect.isEmpty()) {
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	const auto extra = 6;
	auto image = QImage(
		(rect.width() + extra * 2) * ratio,
		(rect.height() + extra * 2) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = QPainter(&image);
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		auto fill = color;
		fill.setAlphaF(0.9);
		q.setBrush(fill);
		const auto radius = rect.height() / 3.;
		q.drawRoundedRect(
			QRectF(extra - 1, extra, rect.width() + 2, rect.height()),
			radius,
			radius);
	}
	image = BlurImage(std::move(image), 3 * ratio);
	image.setDevicePixelRatio(ratio);
	p.drawImage(rect.topLeft() - QPoint(extra, extra), image);
}

int GradientPresetsCount() {
	return 6;
}

QLinearGradient GradientPreset(int index, const QRect &rect) {
	auto gradient = QLinearGradient(rect.topLeft(), rect.bottomRight());
	switch (index) {
	case 1:
		gradient.setColorAt(0., QColor(0x8E, 0x2D, 0xE2));
		gradient.setColorAt(1., QColor(0xE1, 0x00, 0xFF));
		break;
	case 2:
		gradient.setColorAt(0., QColor(0x21, 0x93, 0xB0));
		gradient.setColorAt(1., QColor(0x6D, 0xD5, 0xED));
		break;
	case 3:
		gradient.setColorAt(0., QColor(0xFF, 0x51, 0x2F));
		gradient.setColorAt(1., QColor(0xF0, 0x98, 0x19));
		break;
	case 4:
		gradient.setColorAt(0., QColor(0x11, 0x99, 0x8E));
		gradient.setColorAt(1., QColor(0x38, 0xEF, 0x7D));
		break;
	case 5:
		gradient.setColorAt(0., QColor(0x0F, 0x20, 0x27));
		gradient.setColorAt(1., QColor(0x2C, 0x53, 0x64));
		break;
	case 6:
		gradient.setColorAt(0., QColor(0xEE, 0x9C, 0xA7));
		gradient.setColorAt(1., QColor(0xFF, 0xDD, 0xE1));
		break;
	default:
		gradient.setColorAt(0., makeDefaultBackgroundColor());
		gradient.setColorAt(1., makeDefaultBackgroundColor());
		break;
	}
	return gradient;
}

namespace {

constexpr auto kCardRadius = 16;
constexpr auto kCardPadding = 10;
constexpr auto kCardSpacing = 12;
constexpr auto kOuterPadding = 32;
constexpr auto kShadowRadius = 18;
constexpr auto kCodeHeaderHeight = 38;
constexpr auto kCodeDotRadius = 6;
constexpr auto kCodeDotSpacing = 22;

struct RenderedPart {
	QImage image;
	bool attachedToPrevious = false;
};

[[nodiscard]] QRect ContentBounds(const QImage &image) {
	auto minX = image.width();
	auto minY = image.height();
	auto maxX = -1;
	auto maxY = -1;
	for (auto y = 0; y != image.height(); ++y) {
		const auto line = reinterpret_cast<const uint32*>(image.constScanLine(y));
		for (auto x = 0; x != image.width(); ++x) {
			if (line[x] >> 24) {
				minX = std::min(minX, x);
				maxX = std::max(maxX, x);
				minY = std::min(minY, y);
				maxY = std::max(maxY, y);
			}
		}
	}
	return (maxX < 0) ? QRect() : QRect(minX, minY, maxX - minX + 1, maxY - minY + 1);
}

[[nodiscard]] QColor CardColor() {
	return Window::Theme::IsNightMode()
		? st::boxBg->c.lighter(130)
		: st::boxBg->c;
}

[[nodiscard]] QImage MakeShadow(QSize size, int radius, int ratio) {
	const auto grow = kShadowRadius * 2;
	auto shadow = QImage(
		(size.width() + grow) * ratio,
		(size.height() + grow) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	shadow.setDevicePixelRatio(ratio);
	shadow.fill(Qt::transparent);
	{
		auto q = QPainter(&shadow);
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		q.setBrush(QColor(0, 0, 0, 110));
		q.drawRoundedRect(
			QRect(kShadowRadius, kShadowRadius + 4, size.width(), size.height()),
			radius,
			radius);
	}
	return BlurImage(std::move(shadow), kShadowRadius * ratio / 2);
}

void PaintCard(
		QPainter &p,
		const QRect &rect,
		int radius,
		const QColor &color) {
	const auto ratio = style::DevicePixelRatio();
	const auto shadow = MakeShadow(rect.size(), radius, ratio);
	p.drawImage(rect.topLeft() - QPoint(kShadowRadius, kShadowRadius), shadow);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(color);
	p.drawRoundedRect(rect, radius, radius);
}

void PaintBackground(QPainter &p, const QRect &rect, int gradientPreset) {
	if (gradientPreset > 0) {
		p.fillRect(rect, GradientPreset(gradientPreset, rect));
	} else {
		p.fillRect(rect, makeDefaultBackgroundColor());
	}
}

[[nodiscard]] QImage ComposeClassic(
		const std::vector<RenderedPart> &parts,
		bool showBackground,
		int gradientPreset) {
	const auto ratio = style::DevicePixelRatio();
	auto width = 0;
	auto height = 0;
	for (const auto &part : parts) {
		width = std::max(width, int(part.image.width() / ratio));
		height += part.image.height() / ratio;
	}
	if (!width || !height) {
		return {};
	}
	auto stacked = QImage(width * ratio, height * ratio, QImage::Format_ARGB32_Premultiplied);
	stacked.setDevicePixelRatio(ratio);
	stacked.fill(Qt::transparent);
	{
		auto q = QPainter(&stacked);
		auto y = 0;
		for (const auto &part : parts) {
			q.drawImage(0, y, part.image);
			y += part.image.height() / ratio;
		}
	}
	auto result = addPadding(stacked);
	if (!showBackground) {
		return result;
	}
	auto withBackground = QImage(result.size(), QImage::Format_ARGB32_Premultiplied);
	withBackground.setDevicePixelRatio(ratio);
	withBackground.fill(Qt::transparent);
	{
		auto q = QPainter(&withBackground);
		PaintBackground(q, QRect(QPoint(), result.size() / ratio), gradientPreset);
		q.drawImage(0, 0, result);
	}
	return withBackground;
}

[[nodiscard]] QImage ComposeCards(
		const std::vector<RenderedPart> &parts,
		int gradientPreset) {
	const auto ratio = style::DevicePixelRatio();
	struct Card {
		std::vector<const RenderedPart*> parts;
		int width = 0;
		int height = 0;
	};
	auto cards = std::vector<Card>();
	for (const auto &part : parts) {
		if (cards.empty() || !part.attachedToPrevious) {
			cards.push_back({});
		}
		auto &card = cards.back();
		card.parts.push_back(&part);
		card.width = std::max(card.width, int(part.image.width() / ratio));
		card.height += part.image.height() / ratio;
	}
	auto contentWidth = 0;
	auto contentHeight = 0;
	for (const auto &card : cards) {
		contentWidth = std::max(contentWidth, card.width + kCardPadding * 2);
		contentHeight += card.height + kCardPadding * 2;
	}
	contentHeight += kCardSpacing * std::max(int(cards.size()) - 1, 0);
	const auto totalWidth = contentWidth + kOuterPadding * 2;
	const auto totalHeight = contentHeight + kOuterPadding * 2;
	auto result = QImage(totalWidth * ratio, totalHeight * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto q = QPainter(&result);
	PaintBackground(q, QRect(0, 0, totalWidth, totalHeight), gradientPreset);
	auto y = kOuterPadding;
	for (const auto &card : cards) {
		const auto rect = QRect(
			kOuterPadding,
			y,
			contentWidth,
			card.height + kCardPadding * 2);
		PaintCard(q, rect, kCardRadius, CardColor());
		auto partY = rect.y() + kCardPadding;
		for (const auto part : card.parts) {
			q.drawImage(rect.x() + kCardPadding, partY, part->image);
			partY += part->image.height() / ratio;
		}
		y += rect.height() + kCardSpacing;
	}
	return result;
}

[[nodiscard]] QImage ComposeCodeWindow(
		const std::vector<RenderedPart> &parts,
		int gradientPreset) {
	const auto ratio = style::DevicePixelRatio();
	auto width = 0;
	auto height = 0;
	for (const auto &part : parts) {
		width = std::max(width, int(part.image.width() / ratio));
		height += part.image.height() / ratio;
	}
	const auto cardWidth = std::max(width, kCodeDotSpacing * 4) + kCardPadding * 2;
	const auto cardHeight = height + kCardPadding * 2 + kCodeHeaderHeight;
	const auto totalWidth = cardWidth + kOuterPadding * 2;
	const auto totalHeight = cardHeight + kOuterPadding * 2;
	auto result = QImage(totalWidth * ratio, totalHeight * ratio, QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	auto q = QPainter(&result);
	PaintBackground(
		q,
		QRect(0, 0, totalWidth, totalHeight),
		gradientPreset ? gradientPreset : 1);
	const auto card = QRect(kOuterPadding, kOuterPadding, cardWidth, cardHeight);
	PaintCard(q, card, kCardRadius, CardColor());
	{
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		const auto colors = { QColor(0xFF, 0x5F, 0x56), QColor(0xFF, 0xBD, 0x2E), QColor(0x27, 0xC9, 0x3F) };
		auto x = card.x() + kCardPadding + kCodeDotRadius + 4;
		const auto cy = card.y() + kCodeHeaderHeight / 2;
		for (const auto &color : colors) {
			q.setBrush(color);
			q.drawEllipse(QPoint(x, cy), kCodeDotRadius, kCodeDotRadius);
			x += kCodeDotSpacing;
		}
		auto line = CardColor();
		line = Window::Theme::IsNightMode() ? line.lighter(140) : line.darker(112);
		q.fillRect(card.x(), card.y() + kCodeHeaderHeight - 1, card.width(), 1, line);
	}
	auto y = card.y() + kCodeHeaderHeight + kCardPadding;
	for (const auto &part : parts) {
		q.drawImage(card.x() + kCardPadding, y, part.image);
		y += part.image.height() / ratio;
	}
	return result;
}

void PaintShotUserpic(
		Painter &p,
		not_null<HistoryItem*> message,
		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> &userpics,
		int x,
		int y,
		int outerWidth,
		int size,
		bool paused) {
	const auto &shot = AyuSettings::getInstance().messageShotSettings();
	const auto mode = shot.avatarMode();
	if (mode == 3) {
		return;
	}
	const auto from = message->displayFrom();
	const auto info = from ? nullptr : message->displayHiddenSenderInfo();
	if (!from && !info) {
		return;
	}
	const auto paintOriginal = [&](Painter &q, int px, int py) {
		if (from) {
			Dialogs::Ui::PaintUserpic(
				q,
				from,
				nullptr,
				userpics[from],
				px,
				py,
				outerWidth,
				size,
				paused);
		} else if (info->customUserpic.empty()) {
			info->emptyUserpic.paintCircle(q, px, py, outerWidth, size);
		}
	};
	if (mode == 1) {
		const auto name = from ? DisplayNameFor(from) : info->name;
		const auto colorIndex = from
			? from->colorIndex()
			: Ui::EmptyUserpic::ColorIndex(0);
		Ui::EmptyUserpic(
			Ui::EmptyUserpic::UserpicColor(colorIndex),
			name
		).paintCircle(p, x, y, outerWidth, size);
		return;
	} else if (mode == 2) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(Window::Theme::IsNightMode()
			? QColor(0x4A, 0x4A, 0x4A)
			: QColor(0xBD, 0xBD, 0xBD));
		p.drawEllipse(QRect(x, y, size, size));
		return;
	}
	if (!shot.blurAvatars()) {
		paintOriginal(p, x, y);
		return;
	}
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(size * ratio, size * ratio, QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto q = Painter(&image);
		paintOriginal(q, 0, 0);
	}
	image = BlurImage(std::move(image), size * ratio / 4);
	auto masked = QImage(size * ratio, size * ratio, QImage::Format_ARGB32_Premultiplied);
	masked.setDevicePixelRatio(ratio);
	masked.fill(Qt::transparent);
	{
		auto q = QPainter(&masked);
		auto hq = PainterHighQualityEnabler(q);
		q.setPen(Qt::NoPen);
		q.setBrush(Qt::white);
		q.drawEllipse(QRect(0, 0, size, size));
		q.setCompositionMode(QPainter::CompositionMode_SourceIn);
		q.drawImage(0, 0, image);
	}
	p.drawImage(x, y, masked);
}

}

void Make(not_null<QWidget*> box, const ShotConfig &config, const Fn<void(QImage&,bool)>& callback) {
	const auto controller = config.controller;
	const auto st = config.st;
	auto messages = config.messages;

	if (messages.empty()) {
		return;
	}

	auto delegate = std::make_shared<MessageShotDelegate>(
		box,
		st.get(),
		[=]
		{
			box->update();
		},
		messages.front()->history());

	// remove deleted messages
	messages.erase(
		std::ranges::remove_if(
			messages,
			[=](const auto &message)
			{
				return !message || !controller->session().data().message(message->fullId());
			}).begin(),
		messages.end()
	);

	if (messages.empty()) {
		return;
	}

	auto createdViews = std::make_shared<std::unordered_map<not_null<HistoryItem*>, std::shared_ptr<HistoryView::Element>>>();
	createdViews->reserve(messages.size());
	for (const auto &message : messages) {
		createdViews->emplace(message, message->createView(delegate.get()));
	}

	auto getView = [createdViews](not_null<HistoryItem*> msg)
	{
		return createdViews->at(msg).get();
	};

	// recalculate blocks
	if (messages.size() > 1) {
		auto currentMsg = messages[0].get();

		for (auto i = 1; i != messages.size(); ++i) {
			const auto nextMsg = messages[i].get();
			if (getView(nextMsg)->isHidden()) {
				getView(nextMsg)->setDisplayDate(false);
			} else {
				const auto viewDate = getView(currentMsg)->dateTime();
				const auto nextDate = getView(nextMsg)->dateTime();
				getView(nextMsg)->setDisplayDate(nextDate.date() != viewDate.date());
				auto attached = getView(nextMsg)->computeIsAttachToPrevious(getView(currentMsg));
				getView(nextMsg)->setAttachToPrevious(attached, getView(currentMsg));
				getView(currentMsg)->setAttachToNext(attached, getView(nextMsg));
				currentMsg = nextMsg;
			}
		}

		getView(messages[messages.size() - 1])->setAttachToNext(false);
	} else {
		getView(messages[0])->setAttachToPrevious(false);
		getView(messages[0])->setAttachToNext(false);
	}

	struct MediaPreload {
		std::vector<std::shared_ptr<Data::PhotoMedia>> photos;
		std::vector<std::shared_ptr<Data::DocumentMedia>> documents;
	};
	auto preload = std::make_shared<MediaPreload>();

	for (const auto &message : messages) {
		if (!message->media()) continue;
		const auto origin = Data::FileOrigin(message->fullId());
		if (const auto photo = message->media()->photo()) {
			auto media = photo->activeMediaView()
				? photo->activeMediaView()
				: photo->createMediaView();
			if (!media->loaded()) {
				photo->load(origin, LoadFromCloudOrLocal, false);
			}
			preload->photos.push_back(std::move(media));
		} else if (const auto document = message->media()->document()) {
			auto media = document->activeMediaView()
				? document->activeMediaView()
				: document->createMediaView();
			if (document->hasThumbnail() && !media->thumbnail()) {
				document->loadThumbnail(origin);
			}
			if (document->sticker() && !media->loaded()) {
				document->save(origin, QString());
			}
			preload->documents.push_back(std::move(media));
		}
	}

	const auto &shotSettings = AyuSettings::getInstance().messageShotSettings();
	const auto showBackground = shotSettings.showBackground();
	const auto shotStyle = shotSettings.shotStyle();
	const auto gradientPreset = shotSettings.gradientPreset();
	auto render = [=, messages = std::move(messages), delegate = std::move(delegate)](bool final)
	{
		takingShot = true;

		// calculate the size of the image
		const auto width = st::msgMaxWidth + (st::boxPadding.left() + st::boxPadding.right());
		const auto ratio = style::DevicePixelRatio();

		for (int i = 0; i < messages.size(); i++) {
			const auto &message = messages[i];
			const auto view = getView(message);

			view->itemDataChanged(); // refresh reactions
			view->resizeGetHeight(width);
			if (AyuSettings::getInstance().messageShotSettings().revealSpoilers()) {
				view->revealSpoilers();
			}
		}

		base::flat_map<not_null<PeerData*>, Ui::PeerUserpicView> userpics;

		struct Drawn {
			QImage image;
			QRect bounds;
			bool attachedToPrevious = false;
		};
		auto drawn = std::vector<Drawn>();
		drawn.reserve(messages.size());
		for (int i = 0; i < messages.size(); i++) {
			const auto &message = messages[i];
			const auto view = getView(message);
			const auto height = view->height();
			if (height <= 0) {
				continue;
			}

			QImage image(width * ratio, height * ratio, QImage::Format_ARGB32_Premultiplied);
			image.setDevicePixelRatio(ratio);
			image.fill(Qt::transparent);

			const auto rect = QRect(0, 0, width, height);
			auto context = controller->defaultChatTheme()->preparePaintContext(
				st.get(),
				rect,
				rect,
				rect,
				true);

			{
				Painter p(&image);
				view->draw(p, context);

				const auto displayUserpic = view->displayFromPhoto() || message->isPost();
				if (displayUserpic) {
					PaintShotUserpic(
						p,
						message,
						userpics,
						st::msgMargin.left(),
						height - st::msgPhotoSize,
						width,
						st::msgPhotoSize,
						context.paused);
				}
			}

			const auto bounds = ContentBounds(image);
			if (bounds.isEmpty()) {
				continue;
			}
			drawn.push_back({
				.image = std::move(image),
				.bounds = bounds,
				.attachedToPrevious = (i > 0) && view->isAttachedToPrevious(),
			});
		}

		takingShot = false;

		if (drawn.empty()) {
			return;
		}

		auto left = drawn.front().bounds.left();
		auto right = drawn.front().bounds.right();
		for (const auto &part : drawn) {
			left = std::min(left, part.bounds.left());
			right = std::max(right, part.bounds.right());
		}
		auto parts = std::vector<RenderedPart>();
		parts.reserve(drawn.size());
		for (auto &part : drawn) {
			auto cropped = part.image.copy(QRect(
				left,
				part.bounds.top(),
				right - left + 1,
				part.bounds.height()));
			cropped.setDevicePixelRatio(ratio);
			parts.push_back({
				.image = std::move(cropped),
				.attachedToPrevious = part.attachedToPrevious,
			});
		}

		auto result = (shotStyle == 1)
			? ComposeCards(parts, gradientPreset)
			: (shotStyle == 2)
			? ComposeCodeWindow(parts, gradientPreset)
			: ComposeClassic(parts, showBackground, gradientPreset);
		callback(result, final);
	};

	if (!preload->documents.empty() || !preload->photos.empty()) {
		render(false); // render immediately to give box width

		auto lifetime = std::make_shared<rpl::lifetime>();
		auto latch = std::make_shared<TimedCountDownLatch>(1);
		rpl::single() | rpl::then(
			config.controller->session().downloaderTaskFinished()
		) | rpl::filter([=]
			{
				for (const auto &media : preload->photos) {
					if (media->owner()->loading()) return false;
				}
				for (const auto &media : preload->documents) {
					if (media->owner()->thumbnailLoading()) return false;
					if (media->owner()->sticker() && media->owner()->loading()) return false;
				}
				return true;
			}
		) | rpl::take(1) | rpl::on_next([=]
		{
			latch->countDown();
		}, *lifetime);

		crl::async([=, render = std::move(render)]
		{
			latch->await(std::chrono::seconds(3));
			crl::on_main([=]
			{
				lifetime->destroy();
				render(true);
			});
		});
	} else {
		render(true);
	}
}

namespace {

// 🥀🥀🥀

std::shared_ptr<Ui::ChatStyle> BuildShotChatStyle(
		not_null<Window::SessionController*> controller) {
	const auto &shot = AyuSettings::getInstance().messageShotSettings();
	const auto hasSavedTheme = shot.embeddedThemeType() != -1
		|| shot.cloudThemeId() != 0;
	const auto persistedPalette = getPersistedPalette();
	if (hasSavedTheme && persistedPalette) {
		return std::make_shared<Ui::ChatStyle>(persistedPalette.get());
	}
	return std::make_shared<Ui::ChatStyle>(controller->chatStyle());
}

template <typename ResolveMessage>
void ShowMessageShotBox(
		ResolveMessage resolveMessage,
		not_null<Window::SessionController*> controller,
		const MessageIdsList &ids,
		Fn<void()> clearSelected) {
	auto messages = std::vector<not_null<HistoryItem*>>();
	messages.reserve(ids.size());
	for (const auto item : ids) {
		if (const auto message = resolveMessage(item)) {
			messages.push_back(message);
		}
	}
	if (messages.empty()) {
		return;
	}

	const AyuFeatures::MessageShot::ShotConfig config = {
		controller,
		BuildShotChatStyle(controller),
		messages,
	};
	auto box = Box<MessageShotBox>(config);
	const auto raw = box.data();
	raw->boxClosing() | rpl::on_next([=]
	{
		if (raw->tookShot()) clearSelected();
	}, raw->lifetime());
	Ui::show(std::move(box));
}

template <typename Widget, typename GetIds>
void WrapperImpl(
		not_null<Widget*> widget,
		GetIds getIds,
		Fn<void()> clearSelected) {
	const auto items = getIds(widget);
	if (items.empty()) {
		return;
	}

	const auto session = &widget->session();
	const auto controller = widget->session().tryResolveWindow();
	if (!controller) {
		return;
	}

	ShowMessageShotBox(
		[=](const auto item) { return session->data().message(item); },
		controller,
		items,
		std::move(clearSelected));
}

}

void Wrapper(not_null<HistoryView::ListWidget*> widget, Fn<void()> clearSelected) {
	WrapperImpl(
		widget,
		[](const auto widget) { return widget->getSelectedIds(); },
		std::move(clearSelected));
}

void Wrapper(not_null<HistoryInner*> widget, Fn<void()> clearSelected) {
	WrapperImpl(
		widget,
		[](const auto widget) { return widget->getSelectedItems(); },
		std::move(clearSelected));
}

}
