param(
    [string]$Version = '2.2.5',
    [string]$OutputDir = '',
    [string]$RuntimeDirectory = '',
    [string]$ManifestPath = '',
    [switch]$KeepIntermediate
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root 'build\installer'
}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$dist = if($RuntimeDirectory){(Resolve-Path -LiteralPath $RuntimeDirectory).Path}else{Join-Path $root 'dist'}
if(-not $ManifestPath){$ManifestPath=Join-Path $root 'release-manifest.json'}
$stage = Join-Path $OutputDir 'payload'
$iss = Join-Path $PSScriptRoot 'lumenplayer.iss'
$icon = Join-Path $root 'resources\LumenPlayer.ico'
$wizardSmall = Join-Path $PSScriptRoot 'wizard-small.bmp'
$language = Join-Path $PSScriptRoot 'ChineseSimplified.isl'
$inno = Join-Path $root 'build\installer\tools\inno-6.7.3\ISCC.exe'
$setupPath = Join-Path $OutputDir "LumenPlayer-$Version-x64-setup.exe"

$innoUrl = 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe'
$innoSha256 = '9C73C3BAE7ED48D44112A0F48E66742C00090BDB5BEF71D9D3C056C66E97B732'
$languageSha256 = 'E0B0B350E2245F3C5E65586DFE43D574F6E7F06F2261149ABA284954B3FC9A8D'

if (-not (Test-Path -LiteralPath $dist)) { throw "dist directory not found: $dist" }
if (-not (Test-Path -LiteralPath $inno)) {
    throw "Inno Setup compiler not found: $inno. Keep build\installer\tools\inno-6.7.3 or reinstall Inno Setup 6.7.3."
}
if (-not (Test-Path -LiteralPath $icon)) { throw "Application icon not found: $icon" }
if (-not (Test-Path -LiteralPath $wizardSmall)) { throw "Wizard image not found: $wizardSmall" }
if (-not (Test-Path -LiteralPath $language)) { throw "Language file not found: $language" }
if ((Get-FileHash -LiteralPath $language -Algorithm SHA256).Hash -ne $languageSha256) {
    throw 'Chinese language file hash mismatch.'
}

$innoLanguages = Join-Path (Split-Path $inno) 'Languages'
New-Item -ItemType Directory -Path $innoLanguages -Force | Out-Null
Copy-Item -LiteralPath $language -Destination (Join-Path $innoLanguages 'ChineseSimplified.isl') -Force

if (Test-Path -LiteralPath $stage) {
    $resolvedStage=(Resolve-Path -LiteralPath $stage).Path
    $resolvedOutput=(Resolve-Path -LiteralPath $OutputDir).Path
    if(-not $resolvedStage.StartsWith($resolvedOutput+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe installer staging directory'}
    if((Get-Item -LiteralPath $resolvedStage).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Installer staging directory is a link'}
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Remove-Item -LiteralPath $setupPath -Force -ErrorAction SilentlyContinue

$manifest=Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if($manifest.version -ne $Version -or $manifest.schemaVersion -ne 2){throw 'Publish and verify the matching model-free release manifest before packaging'}
foreach($record in $manifest.files) {
    if($record.path -notmatch '^dist/[a-zA-Z0-9_+./ -]+$' -or $record.path -match '(^|/)\.\.(/|$)'){throw 'Unsafe runtime manifest path'}
    $relative=$record.path.Substring(5)
    $source=Join-Path $dist $relative
    if((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $record.sha256){throw "Runtime manifest mismatch: $relative"}
    $destination=Join-Path $stage $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath $source -Destination $destination -Force
}

$required = @(
    'LumenPlayer.exe',
    'libmpv-2.dll',
    'LumenVideo.dll',
    'libass-9.dll',
    'caption-cpu.conf',
    'caption-models.json',
    'moonshine.dll',
    'licenses',
    'shaders'
)
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $name))) {
        throw "Payload is missing: $name"
    }
}
foreach ($name in @('LumenPlayer.ini', 'LumenPlayer.log', 'LumenPlaylists.dat', 'cache')) {
    if (Test-Path -LiteralPath (Join-Path $stage $name)) {
        throw "User/runtime file leaked into payload: $name"
    }
}

$payloadFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
if((Get-Item -LiteralPath (Join-Path $stage 'LumenPlayer.exe')).VersionInfo.ProductVersion -ne $Version){throw 'Executable version differs from runtime manifest'}
if(@($payloadFiles | Where-Object {$_.Extension -in @('.ort','.onnx','.gguf','.safetensors')}).Count){throw 'A model weight leaked into the model-free installer'}
$payloadBytes = ($payloadFiles | Measure-Object -Property Length -Sum).Sum
$exeHash = (Get-FileHash -LiteralPath (Join-Path $stage 'LumenPlayer.exe') -Algorithm SHA256).Hash
$iconHash = (Get-FileHash -LiteralPath $icon -Algorithm SHA256).Hash
$wizardHash = (Get-FileHash -LiteralPath $wizardSmall -Algorithm SHA256).Hash

$arguments = @(
    '/Qp',
    "/DAppVersion=$Version",
    "/DStageDir=$stage",
    "/DOutputDir=$OutputDir",
    "/DAppIcon=$icon",
    "/DWizardSmallImage=$wizardSmall",
    $iss
)
$compileOutput = & $inno @arguments 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) {
    Write-Host $compileOutput
    throw 'Inno Setup compilation failed.'
}
$compileOutput -split "`r?`n" | Where-Object { $_ -match '\S' } | Select-Object -Last 4 | ForEach-Object { Write-Host $_ }

if (-not (Test-Path -LiteralPath $setupPath)) {
    throw "Installer was not produced: $setupPath"
}

$setupHash = (Get-FileHash -LiteralPath $setupPath -Algorithm SHA256).Hash
$setupBytes = (Get-Item -LiteralPath $setupPath).Length
$hashLine = $setupHash + '  ' + (Split-Path $setupPath -Leaf) + "`r`n"
[IO.File]::WriteAllText($setupPath + '.sha256', $hashLine, [Text.Encoding]::ASCII)

$provenance = [ordered]@{
    product = 'Lumen Player'
    version = $Version
    builtAt = (Get-Date).ToString('o')
    architecture = 'x64'
    installScope = 'per-user'
    installPath = '%LOCALAPPDATA%\Programs\LumenPlayer'
    installer = Split-Path $setupPath -Leaf
    installerBytes = $setupBytes
    installerSha256 = $setupHash
    exeSha256 = $exeHash
    payloadFiles = $payloadFiles.Count
    payloadBytes = $payloadBytes
    excluded = @('LumenPlayer.ini', 'LumenPlayer.log*', 'LumenPlaylists.dat', 'cache', 'models', 'sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30')
    userDataPolicy = 'Settings, playlists, user subtitles and models survive upgrade and uninstall. Clear user data explicitly outside uninstall when desired.'
    modelPolicy = 'No model weights in installer. User-downloaded and previous model directories survive upgrade and uninstall; remove models explicitly in the application.'
    runtimeStorage = 'Post-install XPRESS8K compression of immutable EXE and DLL assets; unsupported filesystems retain ordinary files.'
    installerTool = 'Inno Setup 6.7.3'
    installerToolSource = $innoUrl
    installerToolSha256 = $innoSha256
    compilerNote = 'Inno Setup 6.7.3 compiler reports non-commercial use only; commercial distribution requires an Inno Setup license.'
    languageFile = 'ChineseSimplified.isl'
    languageSha256 = $languageSha256
    icon = 'resources/LumenPlayer.ico'
    iconSha256 = $iconHash
    wizardSmallImage = 'installer/wizard-small.bmp'
    wizardSmallImageSha256 = $wizardHash
}
[IO.File]::WriteAllText((Join-Path $OutputDir 'provenance.json'),
    ($provenance | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding($false)))

if (-not $KeepIntermediate) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

Write-Host "Installer: $setupPath"
Write-Host ("Size: {0:N2} MB" -f ($setupBytes / 1MB))
Write-Host "SHA256: $setupHash"
