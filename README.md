# Awl Interpretwitch

Read more of your Twitch chat, in Russian.

Interpretwitch is a private OBS dock that detects incoming chat languages and
translates supported messages locally. Choose a small English → Russian model,
or a larger multilingual → Russian model. Keep the originals visible, select and
copy text, and follow translations as they arrive.

This repository contains the chat plugin and its translation helper. Models are
downloaded separately on request and verified against pinned hashes. There is no
cloud translation, telemetry, audio capture or speech generation.

## Status

Version **0.4.0**, a development build for **Apple Silicon, macOS 14+, OBS 32.2.2**.
A Windows x64 build and unsigned installer recipe for OBS 32.2.2 passes the test
suites and a libobs load check, but has not yet been tested inside a live OBS
session. NVIDIA code paths are not yet validated. The current Mac bundle is
ad-hoc signed, not notarized.

Translation runs in a separate process so a model failure can be recovered from
without restarting the dock. The OBS module still contains native UI and network
code; process separation cannot guarantee that OBS will never crash.

## Start here

- [Build and test](BUILD.md), then run `Install Awl Interpretwitch.command`.
- [Set up Twitch and use the dock](chat_plugin/README.md).
- [Trace the source and data flow](chat_plugin/ARCHITECTURE.md).
- [Understand process isolation and its limits](chat_plugin/ISOLATION.md).
- [Inspect dependencies, model provenance and licences](chat_plugin/THIRD_PARTY_NOTICES.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `chat_plugin/src/` | OBS dock, Twitch client, translation helper and HTTPS downloader |
| `chat_plugin/tests/` | Protocol, UI, model verification, localisation and process-failure checks |
| `chat_plugin/data/` | Russian UI strings, pinned model manifests and dependency licences |
| `chat_plugin/scripts/` | Dependency preparation, build, packaging and Mac installation |
| `chat_plugin/CMakeLists.txt` | Native build targets and process boundaries |
| `chat_plugin/dependencies.lock.json` | Build dependencies with URLs, sizes and SHA-256 hashes |

## Licences

Plugin source is **GPL-2.0-or-later**, as marked in its source files and documented
in [chat_plugin/LICENSE](chat_plugin/LICENSE). The existing root [MIT licence](LICENSE)
is retained for repository-level material; it does not replace the plugin's GPL
or third-party licences. Dependency and model terms are listed separately in
[the notices](chat_plugin/THIRD_PARTY_NOTICES.md).

Interpretwitch is independent of OBS Studio and Twitch.
