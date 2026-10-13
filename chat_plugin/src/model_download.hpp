// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace awl {
// Shared HTTPS transport only. Callers own catalogue validation, file writes
// and hash verification; this interface has no audio or inference dependency.
using DownloadSink = std::function<void(const char *, size_t)>;
using ModelFetch =
    std::function<void(const std::string &, const DownloadSink &, const std::atomic<bool> &)>;
void fetch_https(const std::string &url, const DownloadSink &sink,
                 const std::atomic<bool> &cancelled);
} // namespace awl
