# Build and test Interpretwitch

The supported development recipe targets Apple Silicon, macOS 14+, OBS 32.2.2
and Qt 6.11.1. It stages a local package and checks it; installation is a separate
action. No Twitch account or model download is required for the seven unit and
integration test suites.

The experimental bridge suite needs permission to listen on localhost. Sandboxes
that prohibit local sockets must run that suite in an environment allowing them.

## Prerequisites

- Xcode command-line tools and the macOS SDK.
- OBS 32.2.2 installed as `/Applications/OBS.app`.
- Python 3.12 or newer for developer scripts, plus CMake 3.28 or newer.
- The OBS 32.2.2 source headers, matching the installed application.

Provide the OBS source checkout at
`.phase0/research/obs-studio/obs-studio-32.2.2`, or set `AWL_OBS_SOURCE` to its
absolute path. These are build-time headers; Interpretwitch uses the installed
OBS libraries at runtime. No audio-caption project is required.

The script uses `cmake` from `PATH`, an explicit `CMAKE` executable path, or the
local tool at `.phase0/toolchain/cmake/data/bin/cmake`. `ctest` must be installed
beside the chosen CMake executable.

## Build

From the repository root:

```sh
zsh chat_plugin/scripts/build-mac.sh
```

Dependency preparation downloads the checksum-pinned archives listed in
`chat_plugin/dependencies.lock.json`. It extracts a prebuilt CTranslate2 dylib
from its pinned wheel; there is no Python runtime in the installed plugin. The
build compiles Qt WebSockets, SentencePiece, fastText, the dock, helper and tests.
The pinned SIMDe headers provide the SIMD compatibility required by OBS on Mac.

The recipe runs CTest, stages `.phase0/packages/obs-chat-mac/awl-chat.plugin`,
checks the module against installed libobs, then runs the
installer in `--check` mode. It does not install into OBS or touch Twitch settings.

The staged bundle includes plugin source, dependency source archives and licence
notices. `.phase0/packages/obs-chat-mac/package.json` records binary/source hashes
and package size. Ad-hoc signatures verify bundle integrity, not publisher identity.
This recipe is not yet a reproducible-build or independently audited release.

After a successful build, quit OBS and run:

```sh
zsh 'Install Interpretwitch.command'
```

The installer backs up an existing plugin before replacement. Cached models and
settings remain in OBS's configuration directory.

## Checks for changes

Run `ctest --test-dir .phase0/chat/build --output-on-failure` after rebuilding.
The seven suites cover protected text/language routing, settings, localisation,
model-file validation, helper faults and the local browser bridge. Loopback tests
need permission to bind a local port in restricted environments. Diagnostic
executables and fault fixtures are never installed. `-DBUILD_TESTING=OFF` excludes
them from a production-only CMake build; validate releases with tests enabled.

With model files already downloaded and verified, run real inference checks:

```sh
.phase0/chat/build/awl-chat-smoke chat_plugin/data/models.lock.json .phase0/chat/model
.phase0/chat/build/awl-chat-smoke chat_plugin/data/models-heavy.lock.json .phase0/chat/model/m2m100-418m-int8 m2m100-418m-int8
.phase0/chat/build/awl-chat-smoke chat_plugin/data/models-en-fr.lock.json .phase0/chat/model/opus-mt-en-fr-int8 opus-mt-en-fr-int8 fr
.phase0/chat/build/awl-chat-device-check chat_plugin/data/models.lock.json .phase0/chat/model --profiles
```

These commands do not download models. The last check exercises Light → Heavy →
Light and CPU fallback. `--helper /absolute/path/to/awl-chat-worker` checks the
packaged helper. A CUDA-enabled build can use `--require-gpu` to fail unless real
NVIDIA inference succeeds.

## Browser adapter check

With Node.js and Playwright available as developer tools, run:

```sh
node chat_plugin/tests/inline_chat_browser.cjs .phase0/chat/build/awl-chat-inline-tests
```

`PLAYWRIGHT_MODULE` can point at an existing Playwright installation. Chromium
must be installed for that Playwright version. The test uses a temporary browser
context, an offline Twitch-origin page and a real localhost bridge, with delayed
synthetic translations. Node.js and Playwright are not plugin dependencies.

To test the complete bridge with an already cached Light model and real helper:

```sh
.phase0/chat/build/awl-chat-inline-tests --model chat_plugin/data/models.lock.json /absolute/path/to/model/cache /absolute/path/to/awl-chat-worker
```

## Windows

The 0.8.1 Windows build passes the automated suites and a short live OBS session,
but is not yet release-ready. See [Windows release preparation](chat_plugin/RELEASE_WINDOWS.md)
for what was verified and what remains.

The Windows recipe targets x64, Windows 10/11, OBS 32.2.2 installed in
`C:\Program Files\obs-studio` (or set `AWL_OBS_DIR`) and Qt 6.11.1.

Prerequisites, all available through `winget`:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id Python.Python.3.12 -e
winget install --id JRSoftware.InnoSetup -e
```

Then, from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File chat_plugin\scripts\build-windows.ps1
```

`prepare-windows.py` downloads the checksum-pinned archives in
`chat_plugin/dependencies-windows.lock.json` (OBS's Qt 6.11.1 MSVC SDK, the
OBS 32.2.2 source for headers, and the CTranslate2 4.8.2 Windows wheel) plus the
shared source archives from `dependencies.lock.json`. It generates import
libraries for the installed `obs.dll` and `obs-frontend-api.dll` and for the
wheel's `ctranslate2.dll`, so no OBS source build is needed. Pass
`-DAWL_OBS_SDK=ON` to CMake to use an OBS SDK CMake package instead.

The recipe runs CTest, stages `.phase0/packages/obs-chat-win/awl-chat`, checks
the module and helper imports, loads the staged module
in the installed libobs using only OBS's own Qt DLLs, then builds
`.phase0/packages/obs-chat-win/Interpretwitch-<version>-windows-x64-setup.exe`
with Inno Setup. Quit OBS and run that installer. It installs into
`%ProgramData%\obs-studio\plugins\awl-chat` (administrator rights required),
backs up an existing copy to `%ProgramData%\obs-studio\awl-plugin-backups`, and
leaves cached models and settings in `%APPDATA%\obs-studio\plugin_config\awl-chat`.

The package ships `Qt6WebSockets.dll` beside the plugin; base Qt DLLs are OBS's own.
The installer is unsigned. NVIDIA mode needs NVIDIA's CUDA 12 cuBLAS libraries,
which are not part of the package or the graphics driver: users download them from
the settings window (**Download GPU support**, pinned in
`chat_plugin/data/gpu-windows.lock.json`), or an existing CUDA 12 install is used.
On Windows CTest also runs `chat-gpu-fixture` and `chat-gpu-pack`, which exercise
the pack installer against a generated local archive. Gameplay testing remains
outstanding.

The optional real-model bridge check now switches Russian → Heavy English → Light
French through the settings controls. It needs the cached Russian, Heavy and
French models. The model smoke tool accepts a target code after the model ID.
The target-language development checks exercised all five Light packs and Heavy targets ru,
en, fr, ar and ja. All 100 target tokens were checked against the pinned Heavy
vocabulary; translation quality for every direction has not been assessed.

The browser regression checks cover a fixed chat-entry area, pending and
translated row growth, the first overflow, cleanup, and scrolling back while a
translation is pending. They verify live chat stays at the bottom while reading
history and input focus are preserved.

The 0.8.1 browser regression also checks the current Twitch header markup and
five-message catch-up on startup, reconnect, target change and model recovery,
including delayed history population and older rows entering the live work window.
Subsequent live bursts still exercise the 200-row work bound.

The 0.8.0 review passed all seven CTest suites in Release and in an
AddressSanitizer/UndefinedBehaviorSanitizer Debug build (halt on errors; leak
detection disabled on Mac). Prebuilt Qt/CTranslate2 binaries are not fully
instrumented. Additional Chromium tests cover passive status, no idle probe
traffic, script exceptions, socket loss, reply timeout, no replay on recovery,
and continued native chat/input. The real-model bridge check above passed.

The sanitizer build uses a separate directory with `-DAWL_BUILD_PLUGIN=OFF`,
`-DCMAKE_BUILD_TYPE=Debug`, `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`
and `-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`. Run CTest with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Fault fixtures are test-only;
they must never be packaged or run inside OBS.
