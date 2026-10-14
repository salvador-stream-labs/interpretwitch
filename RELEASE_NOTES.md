# Interpretwitch 0.8.3 preview

Local Twitch chat translation in OBS's own Chat dock, by Salvador Stream Labs.
No separate Twitch login, cloud translation or bundled models.

- Light English translation packs or Heavy multilingual translation, with a choice of target language.
- Translation first, optional original text, subtle detected-language pill and background.
- Windows settings can download verified NVIDIA GPU support when needed; CPU remains the default.
- Five-message catch-up, passive health status and an isolated translation helper.
- Graphical Mac `.pkg` installer, with English/Russian instructions, per-user installation and upgrade backups.
- Corrected common English replies such as Yes/No/Bye into the five Light target languages, in both model profiles.
- Safer browser cleanup after layout errors, isolated GPU download staging, and stricter message validation.

## Downloads

Apple Silicon Mac: open `macos-arm64.pkg` (macOS 14+), or use the optional ZIP.
Windows 10/11 x64: use `windows-x64-setup.exe`, or the ZIP for manual installation.
**OBS 32.2.2 is required. Intel Macs and Windows on Arm are not supported.**

Both packages are built from the tagged source in GitHub Actions. Build manifests
link to the exact commit and run. Compare the download's SHA-256 with
`SHA256SUMS.txt`; GitHub CLI can also verify its build attestation:

```sh
gh attestation verify YOUR_DOWNLOAD --repo salvador-stream-labs/interpretwitch
```

See [installation and verification instructions](https://github.com/salvador-stream-labs/interpretwitch/blob/main/DOWNLOADS.md).

## Before publishing this draft

- Review the successful Mac and Windows CI runs, including browser and installer tests.
- Complete Windows runtime redistribution review; included notices are evidence, not a legal clearance.
- Test a browser-downloaded Mac PKG and ZIP through Gatekeeper and installation.
- Run at least one hour of live Windows OBS chat with CPU/GPU, reconnection and restart.

The Windows and Mac installers are unsigned. The Mac bundle is ad-hoc signed and not notarized.
These are preview packages, not a claim of stable certification, independent audit
or a guarantee against crashes. GPU hardware and longer live-stream testing are
outside the hosted CI checks. Keep this release as a draft until the checks above
are completed; signing and wider hardware validation remain stable-release work.
