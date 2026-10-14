// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <optional>

namespace awl::chat {
enum class ModelProfile { Light, Heavy };
// Stable engine identities, independent of UI preset labels and either plugin.
QString model_id(ModelProfile profile);
std::optional<ModelProfile> model_profile(const QString &id);
QString profile_name(ModelProfile profile);
QString profile_description(ModelProfile profile);
QString profile_manifest(const QString &light_manifest, ModelProfile profile);
QString profile_directory(const QString &light_directory, ModelProfile profile);
// Empty means retain the original. Detector confidence is a score, not certainty;
// runner_up is the second-ranked language's score.
QString translation_language(const QString &text, const QString &detected, float confidence,
                             ModelProfile profile, float runner_up = 0.f);
} // namespace awl::chat
