// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_lang_local.h"

#include "lang/lang_instance.h"

namespace AyuLocalLang {
namespace {

const std::vector<std::pair<const char*, const char*>> kRussian = {
	{ "ayu_DeletedMediaUnavailable", "Медиа не было загружено до удаления" },
	{ "ayu_SettingsDeletedSectionTitle", "Удалённые сообщения" },
	{ "ayu_SettingsSaveDeletedInChannels", "Сохранять удалённые в каналах" },
	{ "ayu_SettingsSaveDeletedInComments", "Сохранять удалённые в комментариях" },
	{ "ayu_SettingsSaveDeletedMedia", "Сохранять медиа удалённых" },
	{ "ayu_SettingsSaveDeletedPhotos", "Фото" },
	{ "ayu_SettingsSaveDeletedVideos", "Видео" },
	{ "ayu_SettingsSaveDeletedVoices", "Голосовые" },
	{ "ayu_SettingsSaveDeletedRounds", "Кружки" },
	{ "ayu_SettingsSaveDeletedGifs", "GIF" },
	{ "ayu_SettingsSaveDeletedStickers", "Стикеры" },
	{ "ayu_SettingsSaveDeletedFiles", "Файлы" },
	{ "ayu_SettingsRestoreDeletedInChat", "Удалённые в чате после перезапуска" },
	{ "ayu_SettingsDeletedExclusions", "Исключённые чаты" },
	{ "ayu_SettingsDeletedExclusionsHint", "В этих чатах удалённые сообщения не сохраняются. Добавить или убрать чат можно из меню чата." },
	{ "ayu_SettingsDeletedExclusionsEmpty", "Нет исключённых чатов" },
	{ "ayu_ContextExcludeDeleted", "Не сохранять удалёнки" },
	{ "ayu_ContextIncludeDeleted", "Сохранять удалёнки" },
	{ "ayu_ContextGhostExclude", "Исключить из режима призрака" },
	{ "ayu_ContextGhostInclude", "Вернуть в режим призрака" },
	{ "ayu_ContextNotifyFirstOnly", "Уведомлять только о первом" },
	{ "ayu_ContextNotifyAll", "Уведомлять о каждом" },
	{ "ayu_FiltersHideJoinMessages", "Скрывать сообщения о вступлении" },
	{ "ayu_FiltersHideFromCounter", "Не считать скрытые непрочитанными" },
	{ "ayu_DeletedMediaLimit", "Лимит медиа удалённых" },
	{ "ayu_DeletedMediaNoLimit", "Без лимита" },
	{ "ayu_DeletedMediaKeep", "Хранить медиа удалённых" },
	{ "ayu_DeletedMediaKeepForever", "Всегда" },
	{ "ayu_DeletedMediaKeepWeek", "1 неделю" },
	{ "ayu_DeletedMediaKeepMonth", "1 месяц" },
	{ "ayu_DeletedMediaKeepThreeMonths", "3 месяца" },
	{ "ayu_DeletedMediaKeepHalfYear", "6 месяцев" },
	{ "ayu_DeletedMediaKeepYear", "1 год" },
	{ "ayu_FilterRounds", "Видеосообщения" },
	{ "ayu_DisableBotWidgets", "Отключить виджеты ботов" },
	{ "ayu_WidgetBlockedTitle", "Виджет отключён" },
	{ "ayu_WidgetBlockedText", "Виджеты запускают код бота. Разрешить их для {name}?" },
	{ "ayu_WidgetBlockedAllow", "Разрешить" },
	{ "ayu_ContextWidgetsAllow", "Разрешить виджеты" },
	{ "ayu_ContextWidgetsForbid", "Запретить виджеты" },
	{ "ayu_TranslatorSubtitle", "Перевод сообщений" },
	{ "ayu_ContextCopyChatLog", "Копировать как чат-лог" },
	{ "ayu_ContextClearDeletedSelected", "Очистить удалённые в выбранных" },
	{ "ayu_ClearDeletedSelectedText#one", "Очистить сохранённые удалённые сообщения в {count} чате?" },
	{ "ayu_ClearDeletedSelectedText#few", "Очистить сохранённые удалённые сообщения в {count} чатах?" },
	{ "ayu_ClearDeletedSelectedText#many", "Очистить сохранённые удалённые сообщения в {count} чатах?" },
	{ "ayu_ClearDeletedSelectedText#other", "Очистить сохранённые удалённые сообщения в {count} чатах?" },
	{ "ayu_ClearDeletedInChannels", "Очистить удалённые во всех каналах" },
	{ "ayu_ClearDeletedInChannelsText", "Удалить все сохранённые удалённые сообщения из всех каналов? Группы и личные чаты не затрагиваются." },
	{ "ayu_ClearDeletedInChannelsDone#one", "Очищены удалённые сообщения в {count} канале" },
	{ "ayu_ClearDeletedInChannelsDone#few", "Очищены удалённые сообщения в {count} каналах" },
	{ "ayu_ClearDeletedInChannelsDone#many", "Очищены удалённые сообщения в {count} каналах" },
	{ "ayu_ClearDeletedInChannelsDone#other", "Очищены удалённые сообщения в {count} каналах" },
	{ "ayu_SettingsHideWalletInDrawer", "Скрыть Кошелёк" },
	{ "ayu_SettingsCustomAppName", "Своё имя клиента" },
	{ "ayu_SettingsCustomAppNameHint", "Показывается в заголовке окна и на экране входа. Оставьте пустым для AyuGram." },
	{ "ayu_SettingsCustomAppNamePlaceholder", "AyuGram" },
	{ "ayu_SettingsRoundVideoSize", "Размер видеосообщений" },
	{ "ayu_SettingsRoundVideoSizeHint", "Масштаб кружочков в чате." },
	{ "ayu_SettingsPinnedReactions", "Закреплённые реакции" },
	{ "ayu_SettingsPinnedReactionsChats", "Включить в чатах" },
	{ "ayu_SettingsPinnedReactionsChannels", "Включить в каналах" },
	{ "ayu_SettingsPinnedReactionsChatsList", "Реакции для чатов" },
	{ "ayu_SettingsPinnedReactionsChannelsList", "Реакции для каналов" },
	{ "ayu_SettingsPinnedReactionsHint", "Закреплённые реакции показываются первыми в меню реакций. Введите эмодзи через пробел; в чатах поддерживаются кастомные эмодзи из панели." },
	{ "ayu_PinnedReactionsEditTitle", "Закреплённые реакции" },
	{ "ayu_SettingsHideReplyOnForward", "Скрывать ответы при пересылке" },
	{ "ayu_ForwardHideReply", "Скрыть ответы" },
	{ "ayu_MessageShotUseUsernames", "Показывать юзернеймы" },
	{ "ayu_MessageShotAvatars", "Аватарки" },
	{ "ayu_MessageShotAvatarsNormal", "Обычные" },
	{ "ayu_MessageShotAvatarsInitials", "Инициалы" },
	{ "ayu_MessageShotAvatarsSolid", "Однотонные" },
	{ "ayu_MessageShotAvatarsHidden", "Скрыть" },
	{ "ayu_MessageShotBlurAvatars", "Размыть аватарки" },
	{ "ayu_MessageShotBlurNames", "Размыть имена" },
	{ "ayu_MessageShotStyle", "Стиль" },
	{ "ayu_MessageShotStyleClassic", "Классический" },
	{ "ayu_MessageShotStyleCards", "Карточки" },
	{ "ayu_MessageShotStyleCode", "Окно кода" },
	{ "ayu_MessageShotGradient", "Фон" },
	{ "ayu_MessageShotGradientNone", "Тема" },
	{ "ayu_MessageShotGradientPurple", "Фиолетовый" },
	{ "ayu_MessageShotGradientOcean", "Океан" },
	{ "ayu_MessageShotGradientSunset", "Закат" },
	{ "ayu_MessageShotGradientMint", "Мята" },
	{ "ayu_MessageShotGradientNight", "Ночь" },
	{ "ayu_MessageShotGradientPeach", "Персик" },
	{ "ayu_DeletedJumpToDate", "Перейти к дате" },
	{ "ayu_RemoveMyReactions", "Снять мои реакции" },
	{ "ayu_RemoveMyReactionsText", "Убрать все ваши реакции в этом чате? Сообщения просматриваются по одному, это может занять время." },
	{ "ayu_RemoveMyReactionsAction", "Убрать" },
	{ "ayu_RemoveMyReactionsProgress#one", "Убираю реакции: {count}" },
	{ "ayu_RemoveMyReactionsProgress#few", "Убираю реакции: {count}" },
	{ "ayu_RemoveMyReactionsProgress#many", "Убираю реакции: {count}" },
	{ "ayu_RemoveMyReactionsProgress#other", "Убираю реакции: {count}" },
	{ "ayu_RemoveMyReactionsDone#one", "Убрана {count} реакция" },
	{ "ayu_RemoveMyReactionsDone#few", "Убрано {count} реакции" },
	{ "ayu_RemoveMyReactionsDone#many", "Убрано {count} реакций" },
	{ "ayu_RemoveMyReactionsDone#other", "Убрано {count} реакций" },
};

[[nodiscard]] bool IsRussian() {
	const auto &lang = Lang::GetInstance();
	const auto matches = [](const QString &id) {
		return id.startsWith(u"ru"_q, Qt::CaseInsensitive)
			|| id.startsWith(u"uk"_q, Qt::CaseInsensitive)
			|| id.startsWith(u"be"_q, Qt::CaseInsensitive);
	};
	return matches(lang.id()) || matches(lang.baseId());
}

}

void Apply() {
	if (!IsRussian()) {
		return;
	}
	auto &lang = Lang::GetInstance();
	for (const auto &[key, value] : kRussian) {
		lang.resetValue(QByteArray(key));
		lang.applyValue(QByteArray(key), QByteArray(value));
	}
	lang.updatePluralRules();
}

}
