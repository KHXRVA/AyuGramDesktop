// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/data/deleted_media.h"

#include "core/file_location.h"
#include "core/version.h"
#include "settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "storage/serialize_document.h"
#include "ui/image/image.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>

namespace AyuMessages {
namespace {

constexpr auto kThumbnailSide = 320;

[[nodiscard]] QString MessageFolder(ID userId, ID dialogId) {
	return mediaRootPath()
		+ QString::number(userId)
		+ '/'
		+ QString::number(dialogId)
		+ '/';
}

[[nodiscard]] QString RelativeFromAbsolute(const QString &absolute) {
	const auto root = mediaRootPath();
	return absolute.startsWith(root) ? absolute.mid(root.size()) : absolute;
}

[[nodiscard]] QString AbsoluteFromRelative(const std::string &stored) {
	const auto path = QString::fromStdString(stored);
	if (path.isEmpty() || path == "/") {
		return QString();
	}
	if (QFileInfo(path).isAbsolute()) {
		return path;
	}
	return mediaRootPath() + path;
}

[[nodiscard]] bool WriteBytes(const QString &path, const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return false;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	return file.write(bytes) == bytes.size();
}

[[nodiscard]] QByteArray ReadBytes(const QString &path) {
	if (path.isEmpty()) {
		return {};
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

[[nodiscard]] QByteArray EncodeJpeg(const QImage &image, int quality) {
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, "JPG", quality);
	buffer.close();
	return bytes;
}

[[nodiscard]] QString ExtensionFor(
		not_null<DocumentData*> document,
		const QString &fallback) {
	const auto name = document->filename();
	const auto dot = name.lastIndexOf('.');
	if (dot > 0 && name.size() - dot <= 6) {
		return name.mid(dot);
	}
	const auto mime = document->mimeString();
	if (document->isVoiceMessage()) {
		return u".ogg"_q;
	} else if (document->isVideoMessage()
		|| document->isVideoFile()
		|| mime == u"video/mp4"_q) {
		return u".mp4"_q;
	} else if (document->isGifv()) {
		return u".mp4"_q;
	} else if (mime == u"image/webp"_q) {
		return u".webp"_q;
	} else if (mime == u"application/x-tgsticker"_q) {
		return u".tgs"_q;
	} else if (mime == u"image/jpeg"_q) {
		return u".jpg"_q;
	} else if (mime == u"image/png"_q) {
		return u".png"_q;
	}
	const auto suffix = QMimeDatabase().mimeTypeForName(mime).preferredSuffix();
	return suffix.isEmpty() ? fallback : ('.' + suffix);
}

void SaveThumbnail(
		const QImage &image,
		const QString &basePath,
		AyuMessageBase &message) {
	if (image.isNull()) {
		return;
	}
	const auto scaled = (image.width() > kThumbnailSide
		|| image.height() > kThumbnailSide)
		? image.scaled(
			kThumbnailSide,
			kThumbnailSide,
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation)
		: image;
	const auto path = basePath + u"_thumb.jpg"_q;
	if (WriteBytes(path, EncodeJpeg(scaled, 80))) {
		message.hqThumbPath = RelativeFromAbsolute(path).toStdString();
	}
}

[[nodiscard]] std::vector<char> SerializeDocument(
		not_null<DocumentData*> document) {
	auto bytes = QByteArray();
	{
		auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		Serialize::Document::writeToStream(stream, document);
	}
	return std::vector<char>(bytes.begin(), bytes.end());
}

} // namespace

QString mediaRootPath() {
	return cWorkingDir() + u"tdata/ayu_media/"_q;
}

void saveMediaForMessage(not_null<HistoryItem*> item, AyuMessageBase &message) {
	message.mediaPath = "/";
	message.documentType = int(SavedMediaType::None);
	message.hqThumbPath.clear();
	message.mimeType.clear();
	message.documentSerialized.clear();

	const auto media = item->media();
	if (!media) {
		return;
	}
	const auto folder = MessageFolder(message.userId, message.dialogId);
	const auto base = folder + QString::number(message.messageId);

	if (const auto photo = media->photo()) {
		const auto view = photo->activeMediaView()
			? photo->activeMediaView()
			: photo->createMediaView();
		auto image = QImage();
		for (const auto size : {
				Data::PhotoSize::Large,
				Data::PhotoSize::Thumbnail,
				Data::PhotoSize::Small }) {
			if (const auto loaded = view->image(size)) {
				if (!loaded->isNull()) {
					image = loaded->original();
					break;
				}
			}
		}
		if (image.isNull()) {
			return;
		}
		const auto path = base + u".jpg"_q;
		if (!WriteBytes(path, EncodeJpeg(image, 92))) {
			return;
		}
		message.mediaPath = RelativeFromAbsolute(path).toStdString();
		message.documentType = int(SavedMediaType::Photo);
		message.mimeType = "image/jpeg";
		SaveThumbnail(image, base, message);
		return;
	}

	if (const auto document = media->document()) {
		const auto view = document->activeMediaView()
			? document->activeMediaView()
			: document->createMediaView();
		auto bytes = QByteArray();
		const auto existing = document->filepath(true);
		auto saved = false;
		const auto ext = ExtensionFor(document, u".bin"_q);
		const auto path = base + ext;
		if (!existing.isEmpty() && QFile::exists(existing)) {
			QDir().mkpath(folder);
			if (QFile::exists(path)) {
				QFile::remove(path);
			}
			saved = QFile::copy(existing, path);
		}
		if (!saved) {
			bytes = view->bytes();
			saved = WriteBytes(path, bytes);
		}
		if (!saved) {
			// Nothing local, keep at least the thumbnail so the viewer
			// can show what the message looked like.
			if (const auto thumb = view->thumbnail()) {
				SaveThumbnail(thumb->original(), base, message);
				message.documentType = int(SavedMediaType::Document);
				message.mimeType = document->mimeString().toStdString();
				message.documentSerialized = SerializeDocument(document);
			}
			return;
		}
		message.mediaPath = RelativeFromAbsolute(path).toStdString();
		message.documentType = int(SavedMediaType::Document);
		message.mimeType = document->mimeString().toStdString();
		message.documentSerialized = SerializeDocument(document);
		if (const auto thumb = view->thumbnail()) {
			SaveThumbnail(thumb->original(), base, message);
		} else if (document->isVideoFile()
			|| document->isVideoMessage()
			|| document->isGifv()) {
			if (const auto good = view->goodThumbnail()) {
				SaveThumbnail(good->original(), base, message);
			}
		}
	}
}

PhotoData *restorePhoto(
		not_null<Main::Session*> session,
		const AyuMessageBase &message) {
	if (message.documentType != int(SavedMediaType::Photo)) {
		return nullptr;
	}
	const auto path = AbsoluteFromRelative(message.mediaPath);
	const auto bytes = ReadBytes(path);
	if (bytes.isEmpty()) {
		return nullptr;
	}
	auto reader = QImageReader(path);
	const auto size = reader.size();
	if (size.isEmpty()) {
		return nullptr;
	}
	const auto thumbBytes = ReadBytes(AbsoluteFromRelative(message.hqThumbPath));
	const auto thumbSize = thumbBytes.isEmpty()
		? size.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio)
		: QImageReader(AbsoluteFromRelative(message.hqThumbPath)).size();

	// Stable fake id so that repeated openings reuse the same PhotoData.
	const auto id = PhotoId(0x7000000000000000ULL
		^ (uint64(message.dialogId) << 20)
		^ uint64(message.messageId));

	const auto large = ImageWithLocation{
		.location = ImageLocation(
			{ InMemoryLocation{ bytes } },
			size.width(),
			size.height()),
		.bytes = bytes,
	};
	const auto thumbnail = thumbBytes.isEmpty()
		? large
		: ImageWithLocation{
			.location = ImageLocation(
				{ InMemoryLocation{ thumbBytes } },
				thumbSize.width(),
				thumbSize.height()),
			.bytes = thumbBytes,
		};
	return session->data().photo(
		id,
		0,
		QByteArray(),
		TimeId(message.date),
		0,
		false,
		QByteArray(),
		thumbnail,
		thumbnail,
		large,
		ImageWithLocation(),
		ImageWithLocation(),
		0);
}

DocumentData *restoreDocument(
		not_null<Main::Session*> session,
		const AyuMessageBase &message) {
	if (message.documentType != int(SavedMediaType::Document)) {
		return nullptr;
	}
	auto document = (DocumentData*)nullptr;
	if (!message.documentSerialized.empty()) {
		auto bytes = QByteArray(
			message.documentSerialized.data(),
			int(message.documentSerialized.size()));
		auto stream = QDataStream(&bytes, QIODevice::ReadOnly);
		stream.setVersion(QDataStream::Qt_5_1);
		document = Serialize::Document::readFromStream(
			session,
			AppVersion,
			stream);
	}
	const auto path = AbsoluteFromRelative(message.mediaPath);
	const auto hasFile = !path.isEmpty() && QFile::exists(path);
	if (!document) {
		if (!hasFile) {
			return nullptr;
		}
		const auto id = DocumentId(0x7100000000000000ULL
			^ (uint64(message.dialogId) << 20)
			^ uint64(message.messageId));
		auto attributes = QVector<MTPDocumentAttribute>();
		attributes.push_back(MTP_documentAttributeFilename(
			MTP_string(QFileInfo(path).fileName())));
		document = session->data().document(
			id,
			0,
			QByteArray(),
			TimeId(message.date),
			attributes,
			QString::fromStdString(message.mimeType),
			InlineImageLocation(),
			ImageWithLocation(),
			ImageWithLocation(),
			false,
			0,
			QFileInfo(path).size());
	}
	if (hasFile) {
		document->setLocation(Core::FileLocation(path));
		if (const auto size = QFileInfo(path).size(); size > 0) {
			document->size = size;
		}
	}
	const auto thumbPath = AbsoluteFromRelative(message.hqThumbPath);
	if (!thumbPath.isEmpty()) {
		const auto thumbBytes = ReadBytes(thumbPath);
		const auto thumbSize = QImageReader(thumbPath).size();
		if (!thumbBytes.isEmpty() && !thumbSize.isEmpty()) {
			document->updateThumbnails(
				InlineImageLocation(),
				ImageWithLocation{
					.location = ImageLocation(
						{ InMemoryLocation{ thumbBytes } },
						thumbSize.width(),
						thumbSize.height()),
					.bytes = thumbBytes,
				},
				ImageWithLocation(),
				false);
		}
	}
	return document;
}

void removeSavedMedia(const AyuMessageBase &message) {
	for (const auto &stored : { message.mediaPath, message.hqThumbPath }) {
		const auto path = AbsoluteFromRelative(stored);
		if (!path.isEmpty() && path.startsWith(mediaRootPath())) {
			QFile::remove(path);
		}
	}
}

void removeSavedMediaForDialog(ID userId, ID dialogId) {
	const auto folder = MessageFolder(userId, dialogId);
	if (folder.startsWith(mediaRootPath())) {
		QDir(folder).removeRecursively();
	}
}

} // namespace AyuMessages
