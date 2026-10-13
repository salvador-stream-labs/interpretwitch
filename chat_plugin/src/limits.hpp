// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace awl::chat::limits {
// Shared product limits. Keep independent protocol/file limits distinct even
// when their current values happen to match. Local implementation details stay
// next to the code they govern.
inline constexpr int history_messages = 200;
inline constexpr int queued_translations = 24;
inline constexpr int cached_translations = 256;
inline constexpr int recent_event_ids = 1024;

// QString lengths count UTF-16 code units; these are not byte limits.
inline constexpr int message_characters = 2000;
inline constexpr int translation_characters = 32768;
inline constexpr int status_characters = 2048;
inline constexpr int event_bytes = 65536;
inline constexpr int ipc_frame_bytes = 65536;

// The parent also subtracts queue age from each translation's time budget.
inline constexpr int translation_age_ms = 15000;
inline constexpr int translated_portions = 16;
inline constexpr int input_tokens = 256;
inline constexpr int output_tokens = 256;
} // namespace awl::chat::limits
