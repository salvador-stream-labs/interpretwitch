#!/bin/zsh
set -euo pipefail
awl_chat_root=${0:A:h:h:h}
cd "$awl_chat_root"
awl_chat_cmake=${CMAKE:-${commands[cmake]:-$awl_chat_root/.phase0/toolchain/cmake/data/bin/cmake}}
[[ -x "$awl_chat_cmake" ]] || { print -u2 'Install CMake 3.28+ or set CMAKE to its executable path. See BUILD.md.'; exit 1; }
export CMAKE="$awl_chat_cmake"
awl_chat_obs_source=${AWL_OBS_SOURCE:-$awl_chat_root/.phase0/research/obs-studio/obs-studio-32.2.2}
[[ -f "$awl_chat_obs_source/libobs/obsconfig.h.in" ]] || { print -u2 'Set AWL_OBS_SOURCE to the OBS 32.2.2 source checkout. See BUILD.md.'; exit 1; }
awl_chat_qt="$awl_chat_root/.phase0/chat/deps/qt"
awl_chat_obs=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' /Applications/OBS.app/Contents/Info.plist)
[[ "$awl_chat_obs" == 32.2.2 ]] || { print -u2 'This build recipe pins OBS 32.2.2 and Qt 6.11.1.'; exit 1; }
python3 chat_plugin/scripts/prepare.py
"$awl_chat_cmake" -S .phase0/chat/deps/qtwebsockets -B .phase0/chat/build-qtws \
  -DCMAKE_PREFIX_PATH="$awl_chat_qt" -DCMAKE_INSTALL_PREFIX="$awl_chat_qt" \
  -DCMAKE_BUILD_TYPE=Release -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_USE_CCACHE=OFF \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES=arm64
"$awl_chat_cmake" --build .phase0/chat/build-qtws --parallel 4
"$awl_chat_cmake" --install .phase0/chat/build-qtws
"$awl_chat_cmake" -S chat_plugin -B .phase0/chat/build -DCMAKE_PREFIX_PATH="$awl_chat_qt" \
  -DAWL_OBS_SOURCE="$awl_chat_obs_source" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 -DCMAKE_OSX_ARCHITECTURES=arm64
"$awl_chat_cmake" --build .phase0/chat/build --parallel 4
"${awl_chat_cmake:h}/ctest" --test-dir .phase0/chat/build --output-on-failure
python3 chat_plugin/scripts/package.py
QT_QPA_PLATFORM=offscreen .phase0/chat/build/awl-chat-load-test \
  .phase0/packages/obs-chat-mac/awl-chat.plugin/Contents/MacOS/awl-chat \
  .phase0/packages/obs-chat-mac/awl-chat.plugin/Contents/Resources \
  .phase0/chat/module-test-config
"$awl_chat_root/Install Interpretwitch.command" --check
print 'Quit OBS, then run Install Interpretwitch.command.'
