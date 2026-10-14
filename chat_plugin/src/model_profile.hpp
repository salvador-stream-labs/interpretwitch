// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <optional>

namespace awl::chat {
enum class ModelProfile { Light, Heavy };
// Stable engine identities, independent of UI preset labels and either plugin.
QStringList target_languages(ModelProfile profile);
bool supports_target(ModelProfile profile, const QString &target);
QString language_name(const QString &code);
QJsonObject russian_language_labels();
QString model_id(ModelProfile profile, const QString &target = "ru");
std::optional<ModelProfile> model_profile(const QString &id);
QString profile_name(ModelProfile profile);
QString profile_description(ModelProfile profile, const QString &target = "ru");
QString profile_manifest(const QString &light_manifest, ModelProfile profile,
                         const QString &target = "ru");
QString profile_directory(const QString &light_directory, ModelProfile profile,
                          const QString &target = "ru");
// Empty means retain the original. Detector confidence is a score, not certainty;
// runner_up is the second-ranked language's score.
QString translation_language(const QString &text, const QString &detected, float confidence,
                             ModelProfile profile, float runner_up = 0.f,
                             const QString &target = "ru");
} // namespace awl::chat
