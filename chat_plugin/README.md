# Interpretwitch

An OBS plugin by **Salvador Stream Labs**, version 0.8.3.

## Setup

Connect Twitch in **OBS Settings → Stream** and open **Docks → Chat**. If Chat is
missing, disconnect/reconnect the Twitch account in OBS’s Stream settings. An OBS
HTTP 400 authentication error can prevent its Chat dock from being created.
Interpretwitch does not require a developer account, Client ID or separate login.

Open **Tools → Interpretwitch** (Russian: **Инструменты → Интерпретвич**).
Choose resource usage and **Translate into** (Russian by default), then click **Download model** if it is not already cached. Downloads
are explicit and verified; the package itself contains no model weights.

## Settings

| Control | Effect |
| --- | --- |
| Enable translation | On by default, remembered across launches. Off removes additions, closes the bridge and unloads the helper/model. |
| Reconnect to chat | Retry attachment after opening/reconnecting OBS’s Twitch chat. |
| Resource usage: Light | English chat → Russian, French, German, Spanish or Ukrainian. Approximately 80–84 MB per selected pair; two CPU inference threads. |
| Resource usage: Heavy | Multilingual chat → any of the model’s 100 targets, approximately 497 MB downloaded once. One CPU inference thread. More RAM and work per message. |
| Translate into | Russian by default. Light lists only verified small packs; Heavy lists all supported targets. Already-target-language messages stay unchanged. |
| Run on | CPU by default and recommended for most chats. NVIDIA GPU can be selected on Windows; translation falls back to CPU with a specific reason when GPU cannot be used (see [GPU on Windows](#gpu-on-windows)). Mac GPU is unsupported. |
| Show original | On by default. Shows the smaller original below each translation. Off hides only successfully translated originals and appends the language label inline after the translation. Pending, skipped or failed translations keep the original visible. |
| Show detected language | A subtle label such as АН (English, when translating into Russian) appears before the original, or after the translation when originals are hidden. Hover for the full language name. Detection can be wrong, especially for short or mixed-language messages. |
| Subtle background around translations | A faint theme-aware background encloses each successfully translated original/translation pair. |

Settings apply immediately. Closing the window keeps translation running. The
window is modeless, so OBS remains usable while it is open. Settings use OBS’s chosen language, English or Russian; changes to that UI language
apply after restarting OBS. Pills and full language names in tooltips follow the
translation target. Russian uses distinct two- or three-letter UI abbreviations
(not ISO codes); English uses ISO codes; other targets shorten localized names. Fonts, message timestamps, username colours and the
surrounding chat layout remain Twitch’s responsibility.

Messages already in the target language, unsupported and uncertain messages remain unchanged. A static waiting
label appears during translation. Only bounded recent rows are inspected; busy
chat may skip translations. Text stays selectable, and deleted/hidden rows lose
their added translation. On startup or reconnect, only the latest five existing
messages are eligible for translation. New messages then translate as they arrive.
The same catch-up limit applies after model loading/recovery or a target change,
so restarting OBS does not translate the whole conversation. This is not a transcript recorder.

The small Interpretwitch status below the Stream Chat title shows whether the
adapter is preparing, ready, translating, recovering or unavailable. Hover for
details and the last reply time. It uses existing events, with no idle probes or
animated health spinner. If translation fails, originals stay available. Use
**Reconnect to chat** to retry; downloaded models are reused. The indicator is
not a guarantee about translation accuracy or the health of OBS itself.

For troubleshooting, OBS's log (**Help → Log Files**) contains `[Interpretwitch]`
lines for bridge connections and disconnections, why the Twitch page stopped
translating (an unanswered request, a layout error or an unreadable reply), worker
starts, exits and failures, model state, slow translations and busy-chat skips.
These lines never include chat text, translations, usernames or the bridge token.

Changing target clears old translations, queued work and the previous helper cache.
Only one model is loaded at a time. Heavy reuses the same files for every target;
Light pairs have separate download/cache directories and remain cached for reuse.
Changing the selection never downloads automatically: use **Download model** for
a missing pack. If a Heavy target has no Light pack, switching to Light restores
the last available Light choice (Russian initially). The UI shows the resulting target.

## GPU on Windows

GPU mode needs an NVIDIA GPU, a driver that supports CUDA 12 or later (a newer
driver that advertises CUDA 13 is fine; do not downgrade it) and NVIDIA's CUDA 12
cuBLAS libraries. The graphics driver does not include cuBLAS. When GPU is selected
and cuBLAS is missing, the settings offer **Download GPU support**: a one-time
download of NVIDIA cuBLAS 12.9.1.4 (about 550 MB) directly from NVIDIA, under
NVIDIA's CUDA Toolkit licence. It is checked against a pinned size and SHA-256;
only `cublas64_12.dll`, `cublasLt64_12.dll` and NVIDIA's licence are extracted, into
`%LOCALAPPDATA%\awl-chat\gpu\cublas-12.9.1.4-windows-x86_64` (about 740 MB). No
administrator rights, CUDA Toolkit or system `PATH` change are needed; only the
translation helper uses the files. An existing CUDA 12 installation is used if present.
Delete that folder to remove GPU support. Cancelling the download keeps CPU translation.

GPU is reported ready only after a short test translation succeeds. Otherwise the
settings say why translation uses CPU: GPU support not downloaded, driver too old
(with a link to NVIDIA's drivers), no NVIDIA driver or GPU, not enough GPU memory,
or an unknown error (its text is in the tooltip and OBS log).

## Privacy and upgrades

Chat text travels from Twitch to its existing OBS browser, through a capability-
protected localhost connection, and into the local translation helper. It is not
sent to Salvador Stream Labs or a cloud translation service. Interpretwitch reads
no Twitch passwords, cookies or account tokens, and sends no chat messages.

Existing models and model/device choices are reused. Internal `awl-chat` paths
stay unchanged. The old standalone login, registration guide and separate dock
are gone. Earlier versions may have left their unused sign-in entry in the system
credential store; this version does not read it. The old app authorization can be
revoked on Twitch’s Connections page independently of OBS’s own connection.

See [architecture](ARCHITECTURE.md), [browser adapter limits](INLINE_CHAT.md),
[isolation](ISOLATION.md), [build instructions](../BUILD.md) and
[third-party notices](THIRD_PARTY_NOTICES.md).
