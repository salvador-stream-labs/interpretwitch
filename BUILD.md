# Build and test Interpretwitch

The supported development recipe targets Apple Silicon, macOS 14+, OBS 32.2.2
and Qt 6.11.1. It stages a local package and checks it; installation is a separate
action. No Twitch account or model download is required for the six unit and
integration test suites.

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
checks the module and TLS backend against installed libobs, then runs the
installer in `--check` mode. It does not install into OBS or touch Twitch settings.

The staged bundle includes plugin source, dependency source archives and licence
notices. `.phase0/packages/obs-chat-mac/package.json` records binary/source hashes
and package size. Ad-hoc signatures verify bundle integrity, not publisher identity.
This recipe is not yet a reproducible-build or independently audited release.

After a successful build, quit OBS and run:

```sh
zsh 'Install Awl Interpretwitch.command'
```

The installer backs up an existing plugin before replacement. Cached models and
settings remain in OBS's configuration directory.

## Checks for changes

Run `ctest --test-dir .phase0/chat/build --output-on-failure` after rebuilding.
The suites cover authentication/protocol handling, selectable chat/history,
model-file validation, helper faults, locale coverage and setup UI. Diagnostic
executables and fault fixtures are never installed. `-DBUILD_TESTING=OFF` excludes
them from a production-only CMake build; validate releases with tests enabled.

With model files already downloaded and verified, run real inference checks:

```sh
.phase0/chat/build/awl-chat-smoke chat_plugin/data/models.lock.json .phase0/chat/model
.phase0/chat/build/awl-chat-smoke chat_plugin/data/models-heavy.lock.json .phase0/chat/model/m2m100-418m-int8 m2m100-418m-int8
.phase0/chat/build/awl-chat-device-check chat_plugin/data/models.lock.json .phase0/chat/model --profiles
```

These commands do not download models. The last check exercises Light → Heavy →
Light and CPU fallback. `--helper /absolute/path/to/awl-chat-worker` checks the
packaged helper. A CUDA-enabled build can use `--require-gpu` to fail unless real
NVIDIA inference succeeds.

## Windows

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
the module and helper imports, loads the staged module and its Schannel TLS
backend in the installed libobs using only OBS's own Qt DLLs, then builds
`.phase0/packages/obs-chat-win/AwlInterpretwitch-<version>-windows-x64-setup.exe`
with Inno Setup. Quit OBS and run that installer. It installs into
`%ProgramData%\obs-studio\plugins\awl-chat` (administrator rights required),
backs up an existing copy to `%ProgramData%\obs-studio\awl-plugin-backups`, and
leaves cached models and settings in `%APPDATA%\obs-studio\plugin_config\awl-chat`.

The package ships `Qt6WebSockets.dll` and the Schannel TLS backend beside the
plugin; base Qt DLLs are OBS's own. The installer is unsigned. NVIDIA mode
additionally needs CUDA 12 runtime libraries (cuBLAS) on `PATH` and a driver;
without them the helper falls back to CPU. NVIDIA execution and gameplay testing
remain outstanding.
