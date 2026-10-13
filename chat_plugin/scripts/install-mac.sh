#!/bin/zsh
set -euo pipefail
awl_chat_root=${0:A:h:h:h}
awl_chat_bundle="$awl_chat_root/.phase0/packages/obs-chat-mac/awl-chat.plugin"
awl_chat_plugins="$HOME/Library/Application Support/obs-studio/plugins"
awl_chat_target="$awl_chat_plugins/awl-chat.plugin"
awl_chat_pending=""
awl_chat_backup=""
awl_chat_check=0
if [[ ${1:-} == --check ]]; then awl_chat_check=1; shift; fi
(( $# == 0 )) || { print -u2 'Usage: Install Awl Interpretwitch.command [--check]'; exit 2; }
finish() {
  local result=$?
  if [[ -n "$awl_chat_pending" && -d "$awl_chat_pending" ]]; then /bin/rm -rf -- "$awl_chat_pending"; fi
  if (( result != 0 )) && [[ -n "$awl_chat_backup" && ! -e "$awl_chat_target" ]]; then /bin/mv "$awl_chat_backup" "$awl_chat_target"; fi
  if [[ -t 0 ]] && (( ! awl_chat_check )); then read -r '?Press Return to close. '; fi
}
trap finish EXIT
[[ $(uname -m) == arm64 ]] || { print -u2 'This package requires an Apple Silicon Mac.'; exit 1; }
awl_chat_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' /Applications/OBS.app/Contents/Info.plist)
[[ "$awl_chat_version" == 32.2.2 ]] || { print -u2 'This development package is built for OBS 32.2.2.'; exit 1; }
[[ -f "$awl_chat_bundle/Contents/MacOS/awl-chat" ]] || { print -u2 'Build the package first with chat_plugin/scripts/build-mac.sh.'; exit 1; }
[[ -x "$awl_chat_bundle/Contents/MacOS/awl-chat-worker" ]] || { print -u2 'Translation worker is missing. Rebuild or download the complete package.'; exit 1; }
codesign --verify --deep --strict "$awl_chat_bundle"
lipo "$awl_chat_bundle/Contents/MacOS/awl-chat" -verify_arch arm64
[[ ! -L "$awl_chat_bundle" && -z $(find "$awl_chat_bundle" -type l -print -quit) ]] || { print -u2 'Unexpected linked package.'; exit 1; }
if (( awl_chat_check )); then print 'Awl Interpretwitch is ready to install. No files installed.'; exit 0; fi
if /usr/bin/pgrep -x OBS >/dev/null; then
  print -u2 'Quit OBS completely with Command-Q, then run this installer again.'; exit 1
else
  awl_chat_process_status=$?
  (( awl_chat_process_status == 1 )) || { print -u2 'Cannot check OBS. Run this installer in your normal Terminal.'; exit 1; }
fi
mkdir -p "$awl_chat_plugins"
awl_chat_pending=$(mktemp -d "$awl_chat_plugins/.awl-chat-install.XXXXXX")
ditto "$awl_chat_bundle" "$awl_chat_pending/awl-chat.plugin"
codesign --verify --deep --strict "$awl_chat_pending/awl-chat.plugin"
if [[ -e "$awl_chat_target" || -L "$awl_chat_target" ]]; then
  awl_chat_backups="$HOME/Library/Application Support/obs-studio/awl-plugin-backups"
  mkdir -p "$awl_chat_backups"
  awl_chat_backup_dir=$(mktemp -d "$awl_chat_backups/chat.XXXXXX")
  awl_chat_backup="$awl_chat_backup_dir/awl-chat.plugin"
  mv "$awl_chat_target" "$awl_chat_backup"
fi
mv "$awl_chat_pending/awl-chat.plugin" "$awl_chat_target"
print 'Installed. Open OBS → Docks → Awl Interpretwitch.'
print 'Complete Twitch setup, download the model, enter your channel, then Connect Twitch.'
