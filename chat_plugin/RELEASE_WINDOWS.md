# Windows release preparation · Interpretwitch 0.8.2

Status of the Windows x64 build and what remains before it is published. This is
a review document for maintainers; user-facing setup is in [README.md](README.md)
and build steps are in [BUILD.md](../BUILD.md#windows).

## 0.8.2 release automation

The historical Windows measurements below describe 0.8.1. Version 0.8.2 adds
Show original and GitHub build/release preparation; do not treat older live-test
evidence as validation of a new binary.

- Fresh Mac/Windows GitHub builds, browser fixtures, Windows install/upgrade/
  uninstall tests, SHA-256 manifests and provenance attestations are configured
  in `.github/workflows/build.yml`. A version tag creates a draft only, after both
  platforms pass. Inspect that run before publishing.
- cuDNN has been removed from preparation and packaging; the installer now
  restricts installation to x64 Windows (`x64os`).
- Windows CTranslate2, Intel OpenMP, oneDNN and WinHTTP notices are now included.
  The exact OpenMP DLL was matched to Intel's 2025.3.0 wheel. Its EULA and nested
  component notices need redistribution review before publication; this remains
  a gate, not a claim that adding licence files settles all terms.
- The stalled-loading test gives helper startup 1.5 seconds on busy runners while
  still requiring exactly one bounded recovery. Production deadlines are unchanged.
- Source ZIPs now contain tracked files only and record the Git commit and clean
  checkout status. Release wrapping refuses changed module/helper/source hashes.

This addresses the code changes in preview blockers 1–3 below and adds automated
coverage for blocker 5; actual successful CI results must still be reviewed.
The one-hour live acceptance check, publisher signing, broader hardware checks and
backup-retention policy remain outstanding. See [release draft](../RELEASE_NOTES.md).

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
- **Optional GPU support download.** NVIDIA drivers do not include cuBLAS, so most
  users cannot use GPU mode as shipped. When GPU is selected and cuBLAS is missing,
  settings offer **Download GPU support**: NVIDIA's official cuBLAS 12.9.1.4 archive
  (549,755,186 bytes) from `developer.download.nvidia.com`, pinned by size and SHA-256
  in `data/gpu-windows.lock.json`. The helper verifies it, extracts only the two
  DLLs and NVIDIA's licence (each pinned) into
  `%LOCALAPPDATA%\awl-chat\gpu\<version>`, and uses that folder through its own
  `PATH`. No administrator rights, CUDA Toolkit or system change is needed. Before
  GPU is reported ready, the helper checks the driver, CUDA version, device and
  cuBLAS, then runs the existing GPU warm-up translation. Missing libraries, an old
  driver (with a **Get NVIDIA driver** link), no driver or GPU, insufficient GPU
  memory and unknown failures each get their own explanation; CPU stays the default.

## Verification performed

Test machine: Windows 10 Pro 22H2 (build 19045), x64, NVIDIA GeForce GTX 1650
(compute 7.5), driver 617.42, CUDA 12.9 cuBLAS component, OBS 32.2.2.

| Check | Result |
| --- | --- |
| Clean build via `chat_plugin\scripts\build-windows.ps1` | Pass |
| CTest, 9 suites (locale catalogue, localisation, core, UI, model store, process isolation, inline bridge, GPU pack fixture, GPU pack) | 9/9 pass |
| GPU pack tests: verified install, approved-only extraction (including a ZIP64 local header), reuse, damaged pack, tampered archive/files, hostile catalogues (host, scheme, path names), cancellation, error classification | Pass |
| Settings flow with a test helper: GPU selected without cuBLAS shows **Download GPU support** and the licence notice; clicking it requests the pack and ends on GPU | Pass |
| Real flow, packaged helper, environment without CUDA Toolkit: reports missing cuBLAS and translates on CPU; downloads 550 MB from NVIDIA, verifies, extracts and translates on GPU (29 s total here); later starts reuse the pack without downloading | Pass |
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
- Windows 11, machines without NVIDIA, and the GPU support download from inside live
  OBS (verified with the packaged helper in a CUDA-free environment, not yet via the
  settings window in OBS).
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
5. **GPU support download in the field.** Test **Download GPU support** on a
   machine that has never had CUDA, on a slow connection (the helper allows 30
   minutes), with low disk space (about 1.4 GB is needed during installation) and
   with cancellation. Verify the old-driver path and its **Get NVIDIA driver** link
   on a driver older than CUDA 12. Decide whether the pack should be removed on
   uninstall (it lives in `%LOCALAPPDATA%\awl-chat\gpu`, outside the installer's folder).
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
- **Timing-sensitive isolation check.** The `chat-process-isolation` "stalled model
  loading" case uses a 150 ms deadline that includes starting the helper process. It
  failed once while the machine was busy compiling, then passed 6/6 when run alone.
  Make its deadline start when the helper is running, or relax it, before relying on
  it in CI.
- **Catch-up is limited to the latest five messages** when the page or model becomes
  ready (by design, see [INLINE_CHAT.md](INLINE_CHAT.md)). Testers may read this as
  "earlier messages failed to translate".

## Known limitations for release notes

- Windows 10/11 x64 only, OBS 32.2.2 only, OBS's built-in Twitch Chat dock only.
- Unsigned development build (until stable blocker 1).
- CPU memory: Light about 1 GB; Heavy about 2 GB while loading. The helper is limited to 4 GiB.
- GPU requires NVIDIA and a driver supporting CUDA 12 or later (newer drivers are
  fine), plus a one-time optional download of NVIDIA cuBLAS (about 550 MB, about
  740 MB on disk) unless CUDA 12 is already installed. Otherwise CPU is used.
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
