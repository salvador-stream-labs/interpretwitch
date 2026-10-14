# Windows release preparation · Interpretwitch 0.8.1

Status of the Windows x64 build and what remains before it is published. This is
a review document for maintainers; user-facing setup is in [README.md](README.md)
and build steps are in [BUILD.md](../BUILD.md#windows).

## Recommendation

**Not ready for a stable Windows release. Suitable for a clearly labelled preview
(GitHub pre-release) once the [preview blockers](#preview-blockers) are closed.**

The build is reproducible from one script, all automated suites pass on Windows,
and the plugin loads, attaches to OBS's Twitch Chat dock and translates live chat
on CPU and NVIDIA GPU. However, live use on Windows has been short. One early report
of translation stopping was not reproduced or explained. The licence notices do not
yet cover every shipped Windows binary. The installer is unsigned.

## Candidate under review

| Item | Value |
| --- | --- |
| Version | 0.8.1 (`project(awl-chat VERSION 0.8.1)`) |
| Artifact | `Interpretwitch-0.8.1-windows-x64-setup.exe`, about 27 MB, Inno Setup |
| Install location | `%ProgramData%\obs-studio\plugins\awl-chat` (administrator) |
| Settings and models | `%APPDATA%\obs-studio\plugin_config\awl-chat` (kept on upgrade/uninstall) |
| OBS | 32.2.2 exactly (installer gate), Qt 6.11.1 from OBS |
| Toolchain | Visual Studio 2022 Build Tools (MSVC 14.44), CMake, Ninja, Python 3.12, Inno Setup 6 |
| Dependencies | `dependencies.lock.json` + `dependencies-windows.lock.json`, size and SHA-256 pinned |

This candidate includes fixes made during Windows bring-up that should be reviewed
and merged together:

- **Heavy model on CPU failed to load on Windows.** The helper's job-object ceiling
  was 2 GiB of committed memory, and loading M2M100 on CPU peaks near 2 GiB. The
  ceiling is now 4 GiB (`src/helper.cpp`, `ISOLATION.md`). GPU loading was
  unaffected because weights live in VRAM.
- **State-only diagnostics in the OBS log.** `[Interpretwitch]` lines now record
  bridge connects/disconnects with reasons, the page's reason for stopping
  (`timeout`, `layout-error`, `bad-reply`), worker start/exit/deadline/failure,
  model state, slow translations and busy-chat skips. Chat text, translations,
  usernames and the capability token are never logged; `chat-inline-bridge`
  asserts this. Without this there was no way to diagnose live failures.
- **One-word messages.** Previously every single-word message was skipped on Light.
  A short list of common English replies, checked for sensible Light output, now
  translates. Other single Latin words translate only when the detector is at
  least 90% sure they are English. Typed emote names and slang stay unchanged.
- **GPU library discovery.** The supervisor adds `CUDA_PATH_V12_*\bin` to the
  helper's `PATH`, so a cuBLAS-only CUDA 12 install works without editing the
  system `PATH`.

## Verification performed

Test machine: Windows 10 Pro 22H2 (build 19045), x64, NVIDIA GeForce GTX 1650
(compute 7.5), driver 617.42, CUDA 12.9 cuBLAS component, OBS 32.2.2.

| Check | Result |
| --- | --- |
| Clean build via `chat_plugin\scripts\build-windows.ps1` | Pass |
| CTest, 7 suites (locale catalogue, localisation, core, UI, model store, process isolation, inline bridge) | 7/7 pass |
| Package checks: no model weights, no symlinks, OBS module does not import CTranslate2/WinHTTP, every Qt/OBS import is shipped or provided by OBS | Pass |
| Module load in installed libobs with only OBS's Qt DLLs, `en-US` and `ru-RU` | Pass |
| `awl-chat-smoke`, Light (en→ru) and Heavy (M2M100), CPU | Pass |
| `awl-chat-device-check --require-gpu --profiles` with the packaged helper | Pass (GPU used; Light→Heavy→Light; CPU fallback) |
| Heavy prepare + translate on CPU through the packaged helper | Pass after the memory-ceiling fix (about 6 s load) |
| Installer: fresh install, upgrade over 0.4.0 and over 0.8.1 with backup | Pass |
| Live OBS: plugin loads, Tools menu, bridge attaches to the Twitch Chat dock, Light/CPU translates live messages, Reconnect | Pass (short session) |
| GPU without `cudnn64_9.dll` (device check, GPU required) | Pass, so cuDNN is not needed for translation |

Observed memory, private bytes of the helper while loading on CPU: Light about
0.9 GB, Heavy about 1.9 GB (sampled, so the true peak is higher).

### Not yet verified

- `chat_plugin/tests/inline_chat_browser.cjs` (needs Node.js + Playwright on Windows).
- `awl-chat-inline-tests --model …` end to end. It passes through Heavy and back to
  Light, then waits for the Light French pack, which was not downloaded.
- Light packs other than Russian (de, es, fr, uk) on Windows.
- Heavy model in live OBS, GPU mode in live OBS for an extended session, busy chat bursts.
- Uninstall.
- Windows 11, machines without NVIDIA, NVIDIA without CUDA 12 cuBLAS (CPU fallback path in live OBS).
- OBS in Russian UI on Windows.
- BetterTTV/FrankerFaceZ in the OBS Twitch Chat dock.

## Preview blockers

Close these before any public Windows download, including a pre-release.

1. **Merge the candidate fixes above and rebuild from a clean checkout.** Run the
   full recipe on a machine that has not built the project before.
2. **Complete third-party notices for Windows binaries.** `THIRD_PARTY_NOTICES.md`
   describes the CTranslate2 library as the Mac build. The Windows package also
   ships, from the pinned `ctranslate2-4.8.2-cp312-cp312-win_amd64.whl`:
   - `ctranslate2.dll` (MIT, already covered once the text says Windows too),
   - `libiomp5md.dll`, Intel OpenMP runtime, imported by `ctranslate2.dll`, needs
     Intel's redistribution licence text,
   - `cudnn64_9.dll`, NVIDIA cuDNN. It is **not needed** (verified above) and
     should be removed from `package-windows.py` and `prepare-windows.py`, not
     licensed.
   Also note that HTTPS on Windows uses WinHTTP (the notice says macOS).
3. **Restrict the installer to x64 Windows.** `ArchitecturesAllowed=x64compatible`
   also admits Windows on Arm, where OBS is a native Arm64 build that cannot load
   this x64 plugin. Use `x64os`.
4. **Run a live acceptance session with the diagnostics build.** At least one hour of
   real chat: Light and Heavy, CPU and GPU, a target change, Reconnect and an OBS
   restart. The OBS log must show no `stopped translation`, unexpected worker exit
   or `Translation failure` lines. This closes the open report below or captures its cause.
5. **Test uninstall** and confirm it removes `%ProgramData%\obs-studio\plugins\awl-chat`
   while leaving models and settings in `%APPDATA%`.

## Before a stable release

1. **Code signing.** Sign the installer, `awl-chat.dll` and `awl-chat-worker.exe`
   (Authenticode). Unsigned downloads trigger SmartScreen and are more likely to be
   flagged by antivirus, particularly a helper that creates job objects.
2. **OBS version policy.** The installer accepts exactly 32.2.2, and OBS updates
   itself. Decide on a supported range and re-validate on each OBS release. The
   main compatibility risks are the Qt version OBS ships (the plugin needs Qt
   ≥ 6.11), the browser panel ABI (checked at runtime, `obs_browser_qcef_version() == 3`)
   and the libobs import libraries generated from the installed `obs.dll`.
3. **CI.** Add a Windows job (for example GitHub Actions `windows-2022`) that
   installs OBS 32.2.2, runs `build-windows.ps1`, runs the browser adapter test, and
   publishes the installer with SHA-256 checksums from a tag. Today the build depends
   on a developer machine's OBS installation for import libraries.
4. **Back-up pruning.** Each install moves the previous plugin to
   `%ProgramData%\obs-studio\awl-plugin-backups\chat-<timestamp>` and nothing removes
   them. Keep the last one or two, and mention the folder in the uninstall notes.
5. **GPU documentation for users.** GPU mode needs an NVIDIA driver supporting
   CUDA 12 (527.41 or newer on Windows) and the CUDA 12 cuBLAS libraries (for example the cuBLAS
   component of the CUDA 12.x installer). Without them the helper falls back to CPU
   and the settings window says so. CUDA 13 is not compatible (`cublas64_12.dll` is required).
6. **Wider hardware/OS coverage.** Windows 11, an AMD/Intel-GPU-only machine, a
   low-memory machine (8 GB) with Heavy on CPU.

## Open issues

- **Translation stopping (unconfirmed).** In the first live 0.8.1 session, some
  messages were not translated, one was, then translation appeared to stop. The
  helper did not crash or restart. That session predates the diagnostics, so the
  cause is unknown. A later logged session showed no bridge or worker failures, and
  the untranslated messages there were single words, skipped by the rule since changed.
  Treat this as open until blocker 4 passes.
- **One-word translation quality.** Both models translate some lone words poorly
  without context. Light renders "Yes" as «Выполнено» ("Done"); Heavy renders "Bye"
  as «Поздравление» ("Congratulations"). A small per-target override table for the
  handful of affected replies is proposed; it is not implemented.
- **Catch-up is limited to the latest five messages** when the page or model becomes
  ready (by design, see [INLINE_CHAT.md](INLINE_CHAT.md)). Testers may read this as
  "earlier messages failed to translate".

## Known limitations for release notes

- Windows 10/11 x64 only, OBS 32.2.2 only, OBS's built-in Twitch Chat dock only.
- Unsigned development build (until stable blocker 1).
- CPU memory: Light about 1 GB; Heavy about 2 GB while loading. The helper is limited to 4 GiB.
- GPU requires NVIDIA, a CUDA 12 capable driver and CUDA 12 cuBLAS; otherwise CPU is used.
- Twitch page markup changes can stop the adapter; the OBS log records why.

## Bring-up notes

Issues found while bringing up the Windows build, for reviewers of the recipe:

- OBS's Windows Qt SDK has no `offscreen` platform plugin. Headless tests use
  `minimal`; the localisation layout test uses `windows`, because `minimal` lacks
  real font metrics.
- `check_locales.py` read UTF-8 files with the system code page.
- OBS loads plugins with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | DEFAULT_DIRS`, so
  `Qt6WebSockets.dll` beside `awl-chat.dll` loads. The module-load test adds OBS's
  directory with `AddDllDirectory` to match how OBS runs.
- Import libraries for `obs.dll`, `obs-frontend-api.dll` and `ctranslate2.dll` are
  generated from their export tables (`dumpbin`/`lib`), avoiding an OBS source build.
- Short English sentences scored below the original 60% detector threshold despite
  a decisive lead (for example 0.477 vs 0.031). The lead-based rule is now upstream.
