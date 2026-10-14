// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "model_files.hpp"
#include <QString>
#include <string>

namespace awl::chat {
// Optional NVIDIA cuBLAS runtime for GPU mode on Windows. Helper process only:
// OBS never loads CUDA libraries. The pack is a fixed, hash-verified NVIDIA
// archive, downloaded only on request and extracted into a versioned per-user
// folder; only files listed in the catalogue are written.
enum class GpuIssue { None, NoDriver, NoDevice, OldDriver, Libraries, Memory, Unknown };
QString gpu_issue_code(GpuIssue issue);
GpuIssue gpu_issue_from_code(const QString &code, bool *valid = nullptr);
// Classify a CTranslate2/CUDA exception message. Unrecognised errors stay Unknown.
GpuIssue classify_gpu_error(const std::string &message);
// Localised explanation for the settings window. driver_cuda is e.g. 11060.
QString gpu_issue_text(GpuIssue issue, int driver_cuda = 0);

struct GpuProbe {
    GpuIssue issue = GpuIssue::Unknown;
    int driver_cuda = 0;
};
// Driver, device and cuBLAS checks without creating a CUDA context for CTranslate2.
GpuProbe probe_gpu();

// The installed pack directory, or empty if absent/incomplete. Sizes are checked;
// hashes were verified when the pack was installed.
QString installed_gpu_pack(const QString &manifest, const QString &root);
// Download, verify and extract the pack. Returns its directory, or empty if cancelled.
QString install_gpu_pack(const QString &manifest, const QString &root,
                         const std::atomic<bool> &cancel, const Status &status,
                         const Fetch &fetch = {});
// Make the pack's DLLs visible to this process's library loader.
bool use_gpu_pack(const QString &directory);
} // namespace awl::chat
