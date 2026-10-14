// SPDX-License-Identifier: GPL-2.0-or-later
#include "model_profile.hpp"
#include "ui_text.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QFile>
#include <QJsonDocument>
#include <cmath>

static void init_language_catalogue() {
    Q_INIT_RESOURCE(chat_locale);
}

namespace awl::chat {
namespace {
const QJsonObject &languages() {
    static const auto data = [] {
        init_language_catalogue();
        QFile file(":/awl-chat/languages.json");
        if (!file.open(QIODevice::ReadOnly))
            return QJsonObject{};
        return QJsonDocument::fromJson(file.readAll()).object();
    }();
    return data;
}
} // namespace
QStringList target_languages(ModelProfile profile) {
    QStringList result;
    for (auto it = languages().begin(); it != languages().end(); ++it)
        if (profile == ModelProfile::Heavy || it.value().toObject().contains("light_mb"))
            result << it.key();
    return result;
}
bool supports_target(ModelProfile profile, const QString &target) {
    return languages().contains(target) &&
           (profile == ModelProfile::Heavy || languages()[target].toObject().contains("light_mb"));
}
QString language_name(const QString &code) {
    return languages()[code].toObject()[ui_locale() == "ru-RU" ? "ru" : "en"].toString(code);
}
QJsonObject russian_language_labels() {
    QJsonObject labels;
    for (auto it = languages().begin(); it != languages().end(); ++it)
        labels[it.key()] = it.value().toObject()["short_ru"];
    return labels;
}
QString model_id(ModelProfile profile, const QString &target) {
    return profile == ModelProfile::Heavy ? "m2m100-418m-int8" : "opus-mt-en-" + target + "-int8";
}
std::optional<ModelProfile> model_profile(const QString &id) {
    if (id == model_id(ModelProfile::Heavy))
        return ModelProfile::Heavy;
    for (const auto &target : target_languages(ModelProfile::Light))
        if (id == model_id(ModelProfile::Light, target))
            return ModelProfile::Light;
    return {};
}
QString profile_name(ModelProfile profile) {
    return profile == ModelProfile::Heavy ? ui_text("Heavy · Multilingual chat")
                                          : ui_text("Light · English chat");
}
QString profile_description(ModelProfile profile, const QString &target) {
    return profile == ModelProfile::Heavy
               ? ui_text("497 MB download · one model for all target languages. More memory and "
                         "slower translation. Uncertain messages stay unchanged.")
               : ui_text("English → %1. %2 MB download · small memory footprint. Only this "
                         "language pair is downloaded.")
                     .arg(language_name(target))
                     .arg(languages()[target].toObject()["light_mb"].toInt());
}
QString profile_manifest(const QString &light_manifest, ModelProfile profile,
                         const QString &target) {
    if (profile == ModelProfile::Light && target == "ru")
        return light_manifest;
    return QFileInfo(light_manifest)
        .dir()
        .filePath(profile == ModelProfile::Heavy ? "models-heavy.lock.json"
                                                 : "models-en-" + target + ".lock.json");
}
QString profile_directory(const QString &light_directory, ModelProfile profile,
                          const QString &target) {
    // Keep existing Russian/Heavy caches; each new Light pair has its own directory.
    return profile == ModelProfile::Light && target == "ru"
               ? light_directory
               : QDir(light_directory).filePath(model_id(profile, target));
}
QString translation_language(const QString &text, const QString &detected, float confidence,
                             ModelProfile profile, float runner_up, const QString &target) {
    if (text.isEmpty() || !supports_target(profile, target))
        return {};
    static const QRegularExpression cyrillic("[\\x{0400}-\\x{052F}]");
    if (profile == ModelProfile::Light && cyrillic.match(text).hasMatch())
        return {};
    // Common English chat replies the detector scores poorly or not at all on
    // their own (it has no features for words such as "nice" or "same"). This
    // handles routing only; short_replies.cpp corrects a small set of model
    // failures. Ambiguous senses ("right", "agreed") are left out.
    static const QSet<QString> replies{"hello",      "hi",         "hey",      "thanks",
                                       "thank you",  "welcome",    "please",   "sorry",
                                       "bye",        "yes",        "yeah",     "yep",
                                       "no",         "nope",       "ok",       "okay",
                                       "sure",       "maybe",      "this",     "that",
                                       "these",      "what",       "why",      "how",
                                       "who",        "when",       "where",    "here",
                                       "there",      "now",        "later",    "soon",
                                       "again",      "finally",    "wait",     "stop",
                                       "really",     "same",       "true",     "exactly",
                                       "nice",       "wow",        "cool",     "great",
                                       "awesome",    "amazing",    "perfect",  "good",
                                       "bad",        "wrong",      "congrats", "congratulations",
                                       "definitely", "absolutely", "seriously"};
    static const QRegularExpression trailing("[.!?,…]+$");
    if (replies.contains(text.toLower().remove(trailing)))
        return target == "en" ? QString{} : QString("en");
    // Short chat can score below .60 despite a clear lead. Accept a majority
    // score with a 3x lead, or a lower score with a much stronger 10x lead.
    // These detector scores are not calibrated probabilities.
    const bool decisive = confidence >= .60f ||
                          (confidence >= .50f && confidence >= 3 * runner_up) ||
                          (confidence >= .40f && confidence >= 10 * runner_up);
    if (!std::isfinite(confidence) || !std::isfinite(runner_up) || !decisive ||
        detected == target || !languages().contains(detected) ||
        (profile == ModelProfile::Light && detected != "en"))
        return {};
    // A single Latin word is often a username, emote, slang or an ambiguous
    // abbreviation (sheesh, Kappa and gg score .55-.78 for English). Translate one
    // only when the English score is at least .90, e.g. "Definitely".
    // Do not require spaces in scripts such as Chinese, Japanese or Thai.
    static const QRegularExpression latin("[A-Za-z\\x{00C0}-\\x{024F}]");
    if (!text.contains(' ') && (profile == ModelProfile::Light || latin.match(text).hasMatch()) &&
        !(detected == "en" && confidence >= .90f))
        return {};
    return detected;
}
} // namespace awl::chat
