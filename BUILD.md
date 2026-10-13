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

The CMake project has Windows targets, Credential Manager/WinHTTP integration and
helper process limits. A build needs matching OBS/Qt SDKs, CTranslate2 headers,
DLL/import library, SentencePiece and fastText sources. Point `AWL_DEPS`,
`AWL_CT2_LIBRARY`, `AWL_CT2_IMPLIB` and `CMAKE_PREFIX_PATH` at those dependencies.
NVIDIA mode additionally needs compatible CUDA runtime libraries and a driver.

The Mac dependency-preparation script does not prepare a Windows SDK. Windows
compilation, packaging, NVIDIA execution and gameplay testing remain outstanding.
