#!/bin/zsh
set -euo pipefail
awl_chat_root=${0:A:h}
exec /bin/zsh "$awl_chat_root/chat_plugin/scripts/install-mac.sh" "$@"
