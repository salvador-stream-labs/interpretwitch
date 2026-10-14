# Interpretwitch

Local Twitch chat translation by **Salvador Stream Labs**.

Interpretwitch adds local translations to messages in OBS’s
built-in Twitch Chat dock. Twitch keeps its original messages, selectable text,
emotes, appearance and moderation controls. The translation follows the username,
with an optional smaller original underneath, a compact language label and a subtle background.
Turn off **Show original** to append the language label to the translation instead.

Choose **Light · English chat** or **Heavy · Multilingual chat**, then select
**Translate into**. Russian is the default. Light offers five small language packs;
Heavy supports 100 targets with one shared model download.
Translation runs locally in a separate helper process. Models download only on
request, with pinned sizes and hashes. There is no cloud translation, telemetry,
separate Twitch login, Client ID setup, audio capture or speech generation.

## Use it

See [downloads and verification](DOWNLOADS.md) for preview packages and supported
platforms. GitHub builds prepare release drafts; a draft is not a public download.

1. Install the plugin with OBS closed. On Mac, open the `.pkg` installer.
2. Connect Twitch in **OBS Settings → Stream**, then open **Docks → Chat**.
3. Open **Tools → Interpretwitch** to choose/download a model and adjust appearance.

Translation is enabled by default and starts when the Twitch chat page connects.
Closing the settings window keeps it running. Disabling translation removes the
added presentation and unloads the model. The UI follows OBS’s English/Russian
language setting; the Russian product name is **Интерпретвич**.

A small **Interpretwitch · Ready** line in the chat header reports preparation,
recovery or unavailability. Hover for details and the last reply time. It uses
existing events and request deadlines, with no idle probes or animation. A
failure restores original messages and stops waiting indicators.

## Status

Version **0.8.3**, development build for **Apple Silicon, macOS 14+, OBS 32.2.2**.
The preceding browser bridge worked in the owner’s signed-in Mac OBS session.
This revision’s layout, defaults and settings are checked by automated tests;
Windows execution and NVIDIA hardware still need validation for this revision; see
[Windows release preparation](chat_plugin/RELEASE_WINDOWS.md).
Mac development packages use an ad-hoc signature and are not notarized.

Twitch page changes can require adapter updates. Native OBS/Qt code still executes
inside OBS; isolating translation cannot guarantee that OBS will never crash.
See the [bridge limits](chat_plugin/INLINE_CHAT.md) and
[process boundaries](chat_plugin/ISOLATION.md).

## Review and build

- [Build, test and package](BUILD.md).
- [Windows release preparation](chat_plugin/RELEASE_WINDOWS.md).
- [Settings and troubleshooting](chat_plugin/README.md).
- [Source map and data flow](chat_plugin/ARCHITECTURE.md).
- [Dependency and model provenance](chat_plugin/THIRD_PARTY_NOTICES.md).

`chat_plugin/src/` contains the settings window, browser bridge and isolated helper;
`tests/` contains development-only checks; `data/` contains the browser adapter,
localisation, pinned model manifests and licences; `scripts/` contains build and
packaging recipes. Bundles include corresponding source for review.

The stable `awl-chat` binary/config/cache identifiers remain for upgrade
compatibility. They are internal identifiers, not the product brand. The old
standalone dock and its OAuth/EventSub client have been removed.

## Licences

Plugin source is **GPL-2.0-or-later**, as marked in its source files and documented
in [chat_plugin/LICENSE](chat_plugin/LICENSE). The root [MIT licence](LICENSE)
continues to cover repository-level material; it does not replace the plugin or
third-party licences. Interpretwitch is independent of OBS Studio and Twitch.
