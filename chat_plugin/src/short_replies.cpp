// SPDX-License-Identifier: GPL-2.0-or-later
#include "short_replies.hpp"
#include <QRegularExpression>
#include <QStringList>

namespace awl::chat {
QString short_reply(const QString &text, const QString &target) {
    static const QStringList targets{"ru", "fr", "de", "es", "uk"};
    const auto column = targets.indexOf(target);
    if (column < 0)
        return {};
    // Keep this deliberately small: chat replies with a clear meaning, not a
    // word-by-word dictionary. Never match inside a sentence or remove symbols.
    static const QRegularExpression reply(R"(^(\s*)([A-Za-z]+(?: [A-Za-z]+)?)([.!?,…]*)(\s*)$)");
    const auto match = reply.match(text);
    if (!match.hasMatch())
        return {};
    const auto key = "|" + match.captured(2).toLower() + "|";
    static const struct {
        const char *english;
        const char *translations[5]; // Russian, French, German, Spanish, Ukrainian.
    } replies[]{
        {"|yes|yeah|yep|", {"Да", "Oui", "Ja", "Sí", "Так"}},
        {"|no|nope|", {"Нет", "Non", "Nein", "No", "Ні"}},
        {"|hello|hi|hey|", {"Привет", "Bonjour", "Hallo", "Hola", "Привіт"}},
        {"|bye|", {"Пока", "Au revoir", "Tschüss", "Adiós", "Бувай"}},
        {"|thanks|thank you|", {"Спасибо", "Merci", "Danke", "Gracias", "Дякую"}},
        {"|ok|okay|", {"Хорошо", "D’accord", "Okay", "De acuerdo", "Добре"}},
        {"|sorry|", {"Извини", "Pardon", "Entschuldigung", "Perdón", "Вибач"}},
        {"|please|", {"Пожалуйста", "S’il vous plaît", "Bitte", "Por favor", "Будь ласка"}},
    };
    for (const auto &entry : replies)
        if (QString::fromLatin1(entry.english).contains(key))
            return match.captured(1) + QString::fromUtf8(entry.translations[column]) +
                   match.captured(3) + match.captured(4);
    return {};
}
} // namespace awl::chat
