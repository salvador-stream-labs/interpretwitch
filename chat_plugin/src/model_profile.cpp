// SPDX-License-Identifier: GPL-2.0-or-later
#include "model_profile.hpp"
#include "ui_text.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace awl::chat {
QString model_id(ModelProfile profile) {
    return profile == ModelProfile::Heavy ? "m2m100-418m-int8" : "opus-mt-en-ru-int8";
}
std::optional<ModelProfile> model_profile(const QString &id) {
    if (id == model_id(ModelProfile::Light))
        return ModelProfile::Light;
    if (id == model_id(ModelProfile::Heavy))
        return ModelProfile::Heavy;
    return {};
}
QString profile_name(ModelProfile profile) {
    return profile == ModelProfile::Heavy ? ui_text("Heavy · Multilingual → Russian")
                                          : ui_text("Light · English → Russian");
}
QString profile_description(ModelProfile profile) {
    return profile == ModelProfile::Heavy
               ? ui_text(
                     "497 MB download · more memory, slower translation. Supports many languages; "
                     "uncertain messages stay unchanged.")
               : ui_text("English → Russian. 83 MB to download and a small memory footprint. "
                         "Recommended for English chat.");
}
QString profile_manifest(const QString &light_manifest, ModelProfile profile) {
    return profile == ModelProfile::Heavy
               ? QFileInfo(light_manifest).dir().filePath("models-heavy.lock.json")
               : light_manifest;
}
QString profile_directory(const QString &light_directory, ModelProfile profile) {
    // Preserve the existing Light cache on upgrades. Future shared storage can
    // supply another root without changing engine identities or the translator.
    return profile == ModelProfile::Heavy ? QDir(light_directory).filePath(model_id(profile))
                                          : light_directory;
}
QString translation_language(const QString &text, const QString &detected, float confidence,
                             ModelProfile profile, float runner_up) {
    if (text.isEmpty())
        return {};
    static const QRegularExpression cyrillic("[\\x{0400}-\\x{052F}]");
    if (profile == ModelProfile::Light && cyrillic.match(text).hasMatch())
        return {};
    static const QSet<QString> greetings{"hello", "hi", "thanks", "thank you",
                                         "yes",   "no", "bye",    "sorry"};
    if (greetings.contains(text.toLower().remove(QRegularExpression("[.!?]+$"))))
        return "en";
    // M2M100's published ISO language codes. Membership also excludes detector
    // labels for languages this translation model does not support.
    static const QSet<QString> supported = [] {
        const auto codes =
            QStringLiteral(
                "af am ar ast az ba be bg bn br bs ca ceb cs cy da de el en es et fa ff fi fr fy "
                "ga "
                "gd gl gu ha he hi hr ht hu hy id ig ilo is it ja jv ka kk km kn ko lb lg ln lo lt "
                "lv mg mk ml mn mr ms my ne nl no ns oc or pa pl ps pt ro ru sd si sk sl so sq sr "
                "ss su sv sw ta th tl tn tr uk ur uz vi wo xh yi yo zh zu")
                .split(' ');
        return QSet<QString>(codes.begin(), codes.end());
    }();
    // The compressed detector often scores clear, short English below 60% while
    // ranking every other language far lower. Also accept a decisive lead.
    const bool decisive = confidence >= .60f || (confidence >= .40f && confidence >= 10 * runner_up);
    if (!std::isfinite(confidence) || !std::isfinite(runner_up) || !decisive || detected == "ru" ||
        !supported.contains(detected) || (profile == ModelProfile::Light && detected != "en"))
        return {};
    // A single Latin word is often a username, emote or ambiguous abbreviation.
    // Do not require spaces in scripts such as Chinese, Japanese or Thai.
    static const QRegularExpression latin("[A-Za-z\\x{00C0}-\\x{024F}]");
    if (!text.contains(' ') && (profile == ModelProfile::Light || latin.match(text).hasMatch()))
        return {};
    return detected;
}
} // namespace awl::chat
