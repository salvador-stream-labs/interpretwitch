# SPDX-License-Identifier: GPL-2.0-or-later
# Build, test, stage and wrap the Windows x64 plugin in an installer.
# Usage (from any directory): powershell -ExecutionPolicy Bypass -File chat_plugin\scripts\build-windows.ps1
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $root
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')

function Invoke-Checked {
    & $args[0] @($args | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) { throw "Failed ($LASTEXITCODE): $($args -join ' ')" }
}

# Visual Studio x64 developer environment (MSVC, dumpbin, lib, Windows SDK).
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'Install Visual Studio 2022 Build Tools with the C++ workload. See BUILD.md.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ x64 tools are missing. See BUILD.md.' }
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
}

$cmake = if ($env:CMAKE) { $env:CMAKE } else { (Get-Command cmake).Source }
$env:CMAKE = $cmake
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$python = if ($env:PYTHON) { $env:PYTHON } else { (Get-Command py -ErrorAction SilentlyContinue | ForEach-Object { & $_.Source -3.12 -c 'import sys; print(sys.executable)' }) }
if (-not $python) { $python = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe" }
if (-not (Test-Path $python)) { throw 'Install Python 3.12 or newer, or set PYTHON. See BUILD.md.' }
$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'Install Inno Setup 6 (winget install JRSoftware.InnoSetup). See BUILD.md.' }

$obsDir = if ($env:AWL_OBS_DIR) { $env:AWL_OBS_DIR } else { 'C:\Program Files\obs-studio' }
$obsVersion = (Get-Item "$obsDir\bin\64bit\obs64.exe").VersionInfo.ProductVersion
if ($obsVersion -ne '32.2.2') { throw "This build recipe pins OBS 32.2.2 and Qt 6.11.1 (found OBS $obsVersion)." }

$deps = "$root\.phase0\chat\deps-win"
$qt = "$deps\qt"
Invoke-Checked $python chat_plugin\scripts\prepare-windows.py

Invoke-Checked $cmake -S "$deps\qtwebsockets" -B .phase0\chat\build-qtws-win -G Ninja `
    "-DCMAKE_PREFIX_PATH=$qt" "-DCMAKE_INSTALL_PREFIX=$qt" -DCMAKE_BUILD_TYPE=Release `
    -DQT_BUILD_TESTS=OFF -DQT_BUILD_EXAMPLES=OFF -DQT_USE_CCACHE=OFF
Invoke-Checked $cmake --build .phase0\chat\build-qtws-win --parallel
Invoke-Checked $cmake --install .phase0\chat\build-qtws-win

Invoke-Checked $cmake -S chat_plugin -B .phase0\chat\build-win -G Ninja `
    "-DCMAKE_PREFIX_PATH=$qt" "-DAWL_DEPS=$deps" "-DAWL_OBS_SOURCE=$deps\obs-studio" `
    "-DPython3_EXECUTABLE=$python" -DCMAKE_BUILD_TYPE=Release '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
Invoke-Checked $cmake --build .phase0\chat\build-win --parallel

# Tests run against the SDK's Qt and the prepared inference DLLs.
$savedPath = $env:Path
$env:Path = "$qt\bin;$deps\bin;$env:Path"
$env:QT_PLUGIN_PATH = "$qt\plugins"
try { Invoke-Checked $ctest --test-dir .phase0\chat\build-win --output-on-failure }
finally { $env:Path = $savedPath; Remove-Item env:QT_PLUGIN_PATH }

Invoke-Checked $python chat_plugin\scripts\package-windows.py

# Load the staged module in the installed libobs with OBS's own Qt DLLs only.
$package = "$root\.phase0\packages\obs-chat-win\awl-chat"
$savedPath = $env:Path
$env:Path = "$obsDir\bin\64bit;$env:SystemRoot\System32"
$env:QT_PLUGIN_PATH = "$obsDir\bin\64bit"
try {
    foreach ($locale in 'en-US', 'ru-RU') {
        Invoke-Checked .phase0\chat\build-win\awl-chat-load-test.exe "$package\bin\64bit\awl-chat.dll" `
            "$package\data" "$root\.phase0\chat\module-test-config-win" $locale
    }
}
finally { $env:Path = $savedPath; Remove-Item env:QT_PLUGIN_PATH }

$version = (Get-Content .phase0\packages\obs-chat-win\package.json -Raw | ConvertFrom-Json).version
Invoke-Checked $iscc /Q "/DStage=$root\.phase0\packages\obs-chat-win\awl-chat" `
    "/DOutputDir=$root\.phase0\packages\obs-chat-win" "/DAppVersion=$version" chat_plugin\scripts\installer-windows.iss
Write-Host "Installer: $root\.phase0\packages\obs-chat-win\Interpretwitch-$version-windows-x64-setup.exe"
Write-Host 'Quit OBS, then run the installer.'
