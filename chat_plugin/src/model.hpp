// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "chat_core.hpp"
#include "model_profile.hpp"
#include <functional>
#include <memory>
namespace awl::chat {
class Translator {
  public:
    explicit Translator(const QString &directory, RunOn device = RunOn::CPU,
                        ModelProfile profile = ModelProfile::Light);
    ~Translator();
    Translation translate(const Message &message, const std::function<bool()> &cancel);
    void clear_cache();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace awl::chat
