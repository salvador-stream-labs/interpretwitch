// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "model_download.hpp"
#include "model_profile.hpp"
#include <QString>

namespace awl::chat {
// Validate the complete catalogue before touching disk. Downloads are opt-in.
using Status = std::function<void(QString)>;
using Fetch = awl::ModelFetch;
bool prepare_models(const QString &manifest, const QString &directory, bool download,
                    const std::atomic<bool> &cancel, const Status &status, const Fetch &fetch = {},
                    ModelProfile profile = ModelProfile::Light);
} // namespace awl::chat
