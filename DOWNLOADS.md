# Interpretwitch preview downloads

Download only from https://github.com/salvador-stream-labs/interpretwitch/releases.
These builds target **OBS 32.2.2** and its built-in Twitch Chat dock.

| Computer | Download |
| --- | --- |
| Apple Silicon Mac, macOS 14 or later | `Interpretwitch-0.8.3-macos-arm64.pkg` |
| Apple Silicon Mac, optional ZIP installation | `Interpretwitch-0.8.3-macos-arm64.zip` |
| Windows 10/11 x64 | `Interpretwitch-0.8.3-windows-x64-setup.exe` |
| Windows x64, manual installation | `Interpretwitch-0.8.3-windows-x64.zip` |

Intel Macs, Windows on Arm, other OBS versions and other chat services are not
supported by these packages. Models and optional NVIDIA libraries are downloaded
separately from settings. CPU is the default.

## Verify your download

The release includes `SHA256SUMS.txt` and a build JSON file for each platform.
On Mac, put the installer and checksums in the same folder and run:

```sh
shasum -a 256 Interpretwitch-0.8.3-macos-arm64.pkg
```

On Windows, use PowerShell:

```powershell
Get-FileHash .\Interpretwitch-0.8.3-windows-x64-setup.exe -Algorithm SHA256
```

Compare the full result with that file's entry in `SHA256SUMS.txt`. If they differ,
do not install it. SHA-256 identifies the downloaded bytes; it does not prove
that software is harmless or independently audited.

With GitHub CLI installed, verify the build's repository and source provenance:

```sh
gh attestation verify PATH_TO_DOWNLOADED_FILE --repo salvador-stream-labs/interpretwitch
```

Each build JSON records the source commit, build log URL and module/helper/source
hashes. `source.zip` inside the plugin contains the tracked source and dependency
sources. The build is automated, but bit-for-bit independent rebuilds are not
yet guaranteed; runner tools and signatures can affect the output.

## Install

Quit OBS completely first.

**Mac installer:** open the `.pkg` and follow the graphical installer. It installs
only into your account's `Library/Application Support/obs-studio/plugins` folder
and backs up an existing copy. No administrator password is required. Installer
uses your Mac's language, with English and Russian instructions. OBS must be
closed; the installer does not close it for you. Models and settings are retained.

**Mac ZIP:** extract all files together and run `Install Interpretwitch.command`.
Keep `mac-install-common.sh` beside it. Alternatively, copy the whole
`awl-chat.plugin` bundle into the plugin folder above, moving any previous copy
elsewhere first. Do not merge files from different versions.

The current `.pkg` is unsigned. Its plugin bundle has an ad-hoc integrity signature,
not Apple Developer ID notarization. Changing the packaging does not change trust.
macOS can block a downloaded preview; signing and a downloaded-file Gatekeeper
acceptance test remain required before recommending it as a stable release.

**Windows installer:** run the setup EXE. It needs administrator rights to install
into `%ProgramData%\obs-studio\plugins\awl-chat`. It checks the OBS version and
backs up an existing installation. It is currently unsigned; Windows may display
an unknown-publisher warning. Do not disable antivirus or system security settings.

**Windows manual ZIP:** extract it, then copy its `awl-chat` folder into
`%ProgramData%\obs-studio\plugins`. Move an existing copy elsewhere first; do not
mix files from different versions. This installs the same plugin without running
our installer. Windows may require administrator permission for the copy.

Open OBS, connect Twitch in **Settings → Stream**, then open **Docks → Chat**.
Use **Tools → Interpretwitch** to download a model and choose languages and
appearance. Translations remain local and are visible only in your chat dock.

## Remove

Quit OBS. Use Windows Installed Apps if you used the installer; otherwise remove
the `awl-chat` folder (Windows) or `awl-chat.plugin` bundle (Mac) from the plugin
folder above. Models/settings and installation backups are deliberately retained.
The optional Windows GPU cache is `%LOCALAPPDATA%\awl-chat\gpu`; remove it manually
if you no longer want it. No graphics driver or system library is installed.

## Preview limitations

CI checks build/test results, package contents and installation mechanics. It
does not reproduce a long live stream, NVIDIA gameplay contention, every target
language or Twitch extensions. See `chat_plugin/RELEASE_WINDOWS.md` and the release
notes for the outstanding acceptance checks. Helper isolation protects against
many inference faults; no native OBS plugin can guarantee that OBS never crashes.
