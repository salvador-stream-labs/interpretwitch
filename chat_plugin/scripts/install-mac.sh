#!/bin/zsh
set -euo pipefail
source "${0:A:h}/mac-install-common.sh"
awl_chat_root=${0:A:h:h:h}
awl_chat_bundle="$awl_chat_root/.phase0/packages/obs-chat-mac/awl-chat.plugin"
# The downloadable ZIP places this script beside the ready-built bundle.
if [[ -d "${0:A:h}/awl-chat.plugin" ]]; then awl_chat_bundle="${0:A:h}/awl-chat.plugin"; fi
mac_install_paths "$HOME" || { print -u2 'The OBS plugin location is not a regular folder.'; exit 1; }
awl_chat_pending=""
awl_chat_backup=""
awl_chat_check=0
if [[ ${1:-} == --check ]]; then awl_chat_check=1; shift; fi
(( $# == 0 )) || { print -u2 'Usage: Install Interpretwitch.command [--check]'; exit 2; }
finish() {
  local result=$?
  if [[ -n "$awl_chat_pending" && -d "$awl_chat_pending" ]]; then /bin/rm -rf -- "$awl_chat_pending"; fi
  if (( result != 0 )) && [[ -n "$awl_chat_backup" && ! -e "$awl_chat_target" ]]; then /bin/mv "$awl_chat_backup" "$awl_chat_target"; fi
  if [[ -t 0 ]] && (( ! awl_chat_check )); then read -r '?Press Return to close. '; fi
}
trap finish EXIT
mac_check_host
mac_check_bundle "$awl_chat_bundle" || { print -u2 'The plugin bundle is incomplete or invalid. Rebuild or download it again.'; exit 1; }
if (( awl_chat_check )); then print 'Interpretwitch is ready to install. No files installed.'; exit 0; fi
mac_check_obs_closed
mkdir -p "$awl_chat_plugins"
awl_chat_pending=$(mktemp -d "$awl_chat_plugins/.awl-chat-install.XXXXXX")
ditto "$awl_chat_bundle" "$awl_chat_pending/awl-chat.plugin"
codesign --verify --deep --strict "$awl_chat_pending/awl-chat.plugin"
if [[ -e "$awl_chat_target" || -L "$awl_chat_target" ]]; then
  mkdir -p "$awl_chat_backups"
  awl_chat_backup_dir=$(mktemp -d "$awl_chat_backups/chat.XXXXXX")
  awl_chat_backup="$awl_chat_backup_dir/awl-chat.plugin"
  mv "$awl_chat_target" "$awl_chat_backup"
fi
mv "$awl_chat_pending/awl-chat.plugin" "$awl_chat_target"
print 'Installed Interpretwitch by Salvador Stream Labs.'
print 'Open OBS → Tools → Interpretwitch for model and appearance settings.'
print "Translations appear in OBS's Twitch Chat dock. Connect Twitch in OBS Settings → Stream if Chat is missing."
