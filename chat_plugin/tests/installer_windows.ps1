# Installation lifecycle check, only on a disposable GitHub runner.
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true') { throw 'This test requires a disposable GitHub runner.' }
$stage = Join-Path $PWD '.phase0\packages\obs-chat-win'
$version = (Get-Content "$stage\package.json" -Raw | ConvertFrom-Json).version
$installer = "$stage\Interpretwitch-$version-windows-x64-setup.exe"
$plugin = "$env:ProgramData\obs-studio\plugins\awl-chat"
$config = "$env:APPDATA\obs-studio\plugin_config\awl-chat"
if (Test-Path $plugin) { throw 'Runner already contains a plugin; refusing to alter it.' }
# OBS's portable archive is used for the build. Point the installer at that copy.
New-Item 'HKLM:\SOFTWARE\OBS Studio' -Force | Out-Null
Set-Item 'HKLM:\SOFTWARE\OBS Studio' -Value $env:AWL_OBS_DIR
New-Item $config -ItemType Directory -Force | Out-Null
Set-Content "$config\preserve-test.txt" 'settings and models belong to the user'
function Run-Installer($exe) {
    $process = Start-Process $exe -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Installer failed: $($process.ExitCode)" }
}
Run-Installer $installer
if (-not (Test-Path "$plugin\bin\64bit\awl-chat.dll")) { throw 'Fresh install failed' }
Run-Installer $installer
if (-not (Test-Path "$plugin\bin\64bit\awl-chat-worker.exe")) { throw 'Upgrade failed' }
$backups = Get-ChildItem "$env:ProgramData\obs-studio\awl-plugin-backups" -Directory
if (-not $backups) { throw 'Upgrade did not preserve the old installation' }
Run-Installer "$plugin\unins000.exe"
if (Test-Path "$plugin\bin\64bit\awl-chat.dll") { throw 'Uninstall left the module installed' }
if (-not (Test-Path "$config\preserve-test.txt")) { throw 'Uninstall removed user settings' }
Write-Host 'Fresh install, upgrade, backup and uninstall passed; user data preserved.'
