param([switch]$DebugApp)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $argsForBuild = @('android/tools/build_apk.py')
    if ($DebugApp) { $argsForBuild += '--debug' }
    & python @argsForBuild
    if ($LASTEXITCODE -ne 0) { throw 'Android APK build failed.' }
} finally { Pop-Location }
