#!/bin/zsh
# Shared preflight for the graphical package and the optional ZIP installer.
# Sourcing this file performs no checks or writes.
mac_check_host() {
  [[ $(/usr/bin/uname -m) == arm64 ]] || { print -u2 'This package requires an Apple Silicon Mac.'; return 1; }
  local mac_version=$(/usr/bin/sw_vers -productVersion)
  (( ${mac_version%%.*} >= 14 )) || { print -u2 'macOS 14 or later is required.'; return 1; }
  local obs_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' /Applications/OBS.app/Contents/Info.plist)
  [[ "$obs_version" == 32.2.2 ]] || { print -u2 'Install OBS 32.2.2 in Applications first.'; return 1; }
}
mac_check_obs_closed() {
  if /usr/bin/pgrep -x OBS >/dev/null; then
    print -u2 'Quit OBS completely with Command-Q, then run the installer again.'
    return 1
  else
    local result=$?
    (( result == 1 )) || { print -u2 'Cannot check whether OBS is running.'; return 1; }
  fi
}
mac_install_paths() {
  local user_directory=$1 part candidate=$1
  [[ "$user_directory" == /* && "$user_directory" != / && -d "$user_directory" ]] || return 1
  # Do not follow redirected installation folders, including broken symlinks.
  for part in Library 'Application Support' obs-studio; do
    candidate="$candidate/$part"
    [[ ! -L "$candidate" && ( ! -e "$candidate" || -d "$candidate" ) ]] || return 1
  done
  awl_chat_plugins="$candidate/plugins"
  awl_chat_target="$awl_chat_plugins/awl-chat.plugin"
  awl_chat_backups="$candidate/awl-plugin-backups"
  for candidate in "$awl_chat_plugins" "$awl_chat_target" "$awl_chat_backups"; do
    [[ ! -L "$candidate" && ( ! -e "$candidate" || -d "$candidate" ) ]] || return 1
  done
}
mac_check_bundle() {
  local bundle=$1
  [[ -f "$bundle/Contents/MacOS/awl-chat" && -x "$bundle/Contents/MacOS/awl-chat-worker" ]] || return 1
  [[ ! -L "$bundle" && -z $(/usr/bin/find "$bundle" -type l -print -quit) ]] || return 1
  /usr/bin/lipo "$bundle/Contents/MacOS/awl-chat" -verify_arch arm64 || return 1
  /usr/bin/codesign --verify --deep --strict "$bundle"
}
