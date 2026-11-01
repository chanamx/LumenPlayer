param(
    [ValidateSet('Release', 'Test', 'CoreTest', 'ModelTest', 'CaptionTest', 'CaptionRecoveryTest', 'TranslationTest', 'DialogTest', 'ModelUiTest', 'OptimizationTest', 'NanoTest', 'NanoCaptionTest')][string]$Target = 'Release',
    [switch]$Install,
    [string]$OutputDirectory = '',
    [string]$CompilerPath = ''
)
$ErrorActionPreference = 'Stop'
function Copy-BundleAsset($Source,$Target) {
    # Replace the directory entry: staged immutable assets may be hard-linked.
    $temporary=$Target+'.'+[Guid]::NewGuid().ToString('N')+'.copying'
    try {
        Copy-Item -LiteralPath $Source -Destination $temporary -Force
        if(Test-Path -LiteralPath $Target){[IO.File]::Replace($temporary,$Target,[NullString]::Value)}
        else{[IO.File]::Move($temporary,$Target)}
    } finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
function Assert-UcrtImports($Path,$Reader) {
    $imports=(& $Reader '--coff-imports' $Path 2>&1 | Out-String)
    if($LASTEXITCODE -ne 0){throw 'Cannot verify compiled runtime imports'}
    if($imports -match '(?im)^\s*Name:\s*msvcrt\.dll\s*$' -or $imports -notmatch '(?im)^\s*Name:\s*(?:api-ms-win-crt-[^\r\n]+|ucrtbase\.dll)\s*$') {
        throw 'Build rejected: application and native dependencies must use UCRT, matching the pinned release'
    }
}
$previousTemp=$env:TEMP
$previousTmp=$env:TMP
Push-Location $PSScriptRoot
try {
    $compilerTemp=Join-Path $PSScriptRoot 'build/compiler-temp'
    New-Item -ItemType Directory -Path $compilerTemp -Force | Out-Null
    $env:TEMP=$compilerTemp
    $env:TMP=$compilerTemp
    $coreInfo=Get-Content -LiteralPath 'third_party/mpv/build-provenance.json' -Raw -Encoding UTF8 | ConvertFrom-Json
    $recordedToolDirectory=[regex]::Match($coreInfo.compiler,'(?m)^InstalledDir: ([^\r\n]+)').Groups[1].Value
    if(-not $CompilerPath){$CompilerPath=Join-Path $recordedToolDirectory 'g++.exe'}
    $compiler=(Resolve-Path -LiteralPath $CompilerPath -ErrorAction Stop).Path
    $reader=Join-Path $recordedToolDirectory 'llvm-readobj.exe'
    $compilerVersion=(& $compiler '--version' 2>&1 | Out-String)
    if($LASTEXITCODE -ne 0){throw 'The recorded UCRT compiler cannot execute; use the normal Windows build permission context'}
    $outputDir = if($OutputDirectory){[IO.Path]::GetFullPath($(if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $PSScriptRoot $OutputDirectory}))}elseif($Target -eq 'Release'){Join-Path $PSScriptRoot 'build\staging'}else{Join-Path $PSScriptRoot 'build\tests\runtime'}
    $formalDir=Join-Path $PSScriptRoot 'dist'
    if($outputDir.TrimEnd('\') -eq $formalDir -or $outputDir.StartsWith($formalDir+'\',[StringComparison]::OrdinalIgnoreCase)){
        throw 'Build into a staging/test directory; publishing dist is a separate verified step.'
    }
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    $compilerRecord=@{path=$compiler;sha256=(Get-FileHash -LiteralPath $compiler).Hash.ToLowerInvariant();version=$compilerVersion;runtimeFamily='UCRT';reader=$reader}
    [IO.File]::WriteAllText((Join-Path $outputDir 'build-compiler.json'),($compilerRecord|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    if($Target -ne 'Release'){New-Item -ItemType Directory -Path (Join-Path $outputDir '../test/results') -Force | Out-Null}
    $nanoSources=@('src/nanosakura.cpp','src/caption_tokenizer.cpp')
    $nanoLibraries=@('third_party/mpv/deps-ucrt64/ucrt64/lib/libpcre2-8.dll.a','-lnormaliz')
    $archiveLibraries=@('third_party/mpv/deps-ucrt64/ucrt64/lib/libbz2.dll.a')
    if($Target -in @('ModelTest','TranslationTest','DialogTest','ModelUiTest','NanoTest')) {
        $probeSources=@('src/common.cpp','src/caption_models.cpp')
        $probeLibraries=@('-lwinhttp','-lbcrypt','-lshlwapi')+$archiveLibraries
        if($Target -in @('TranslationTest','NanoTest')) {
            $probeSources+=@('src/caption_translation.cpp')+$nanoSources
            $probeLibraries+=@('-lws2_32','-lpsapi')+$nanoLibraries
            if($Target -eq 'NanoTest'){$probeSources+='test/nanosakura_native.cpp';$probeName='NanoSakuraTests.exe'}
            else{$probeSources+='test/caption_translation_integration.cpp';$probeName='CaptionTranslationTests.exe'}
        } elseif($Target -in @('DialogTest','ModelUiTest')) {
            $probeSources+=$(if($Target -eq 'ModelUiTest'){'test/model_ui_native.cpp'}else{'test/caption_dialog_native.cpp'})
            $probeLibraries+=@('-lcomctl32','-luxtheme','-ldwmapi','-lshell32','-lgdi32','-lpsapi')
            $probeName=$(if($Target -eq 'ModelUiTest'){'ModelUiTests.exe'}else{'CaptionDialogTests.exe'})
        } else {$probeSources+='test/caption_model_store.cpp';$probeName='CaptionModelStoreTests.exe'}
        $probeOptimize=if($Target -eq 'ModelUiTest'){'-O2'}else{'-O1'}
        & $compiler '-std=c++17' $probeOptimize '-Wall' '-Wextra' '-Wno-misleading-indentation' '-municode' '-static' '-DLUMEN_TEST' @probeSources @probeLibraries '-o' (Join-Path $outputDir $probeName)
        if($LASTEXITCODE -ne 0){throw 'Caption native test build failed'}
        Assert-UcrtImports (Join-Path $outputDir $probeName) $reader
        $bzRecord=$coreInfo.files | Where-Object {$_.path -eq 'libbz2-1.dll'}
        if(-not $bzRecord -or (Get-FileHash -LiteralPath 'third_party/mpv/runtime/libbz2-1.dll').Hash.ToLowerInvariant() -ne $bzRecord.sha256){throw 'Pinned bzip2 runtime identity mismatch'}
        Copy-BundleAsset 'third_party/mpv/runtime/libbz2-1.dll' (Join-Path $outputDir 'libbz2-1.dll')
        Get-Item -LiteralPath (Join-Path $outputDir $probeName) | Select-Object FullName,Length
        return
    }
    $coreRuntime=Join-Path $PSScriptRoot 'third_party/mpv/runtime'
    if($coreInfo.dependencyFamily -ne 'MSYS2 UCRT64') {throw 'Playback and subtitle libraries must use the verified UCRT64 allocator family'}
    $coreFiles=@($coreInfo.files | Where-Object {$_.path -like '*.dll'})
    foreach($record in $coreFiles) {
        if($record.path -notmatch '^[a-zA-Z0-9_.+-]+\.dll$' -or
            (Get-FileHash -LiteralPath (Join-Path $coreRuntime $record.path)).Hash.ToLowerInvariant() -ne $record.sha256) {
            throw 'Playback runtime artifact verification failed'
        }
    }
    if(@(Get-ChildItem -LiteralPath $coreRuntime -Filter '*.dll' -File).Count -ne $coreFiles.Count) {throw 'Unexpected playback runtime DLLs'}
    $modelName = 'sherpa-onnx-streaming-zipformer-ctc-zh-int8-2025-06-30'
    $fastModel = Join-Path $PSScriptRoot "third_party/models/$modelName/model.int8.ort"
    $modelRecord = Join-Path $PSScriptRoot "third_party/models/$modelName/lumen-model.json"
    if ($Target -ne 'Release' -and (-not (Test-Path -LiteralPath $fastModel) -or -not (Test-Path -LiteralPath $modelRecord))) {
        throw 'Prepare the verified caption model first: python test/prepare_caption_model.py'
    }
    if($Target -ne 'Release') {
    $modelInfo = Get-Content -LiteralPath $modelRecord -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($modelInfo.sourceSha256 -ne '24ffdc19ba9aaed5a6a9beaede1e087745217d82425cf4041bca0c696661801e' -or
        $modelInfo.runtimeSha256 -ne '7f66f939a881baf4f46a2216496798edf4a1429878b646d12674aa62f27d8a25' -or
        $modelInfo.graphOptimizationLevel -ne 'ORT_ENABLE_EXTENDED' -or $modelInfo.layoutOptimizations -or $modelInfo.quantizationChanged -or
        (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'third_party/sherpa-onnx/runtime/onnxruntime.dll')).Hash -ne $modelInfo.runtimeSha256 -or
        (Get-FileHash -LiteralPath $fastModel).Hash -ne $modelInfo.sha256) { throw 'Caption artifact verification failed' }
    }
    $moonInfo=Get-Content -LiteralPath 'third_party/moonshine/provenance.json' -Raw -Encoding UTF8 | ConvertFrom-Json
    if((Get-FileHash -LiteralPath 'third_party/moonshine/runtime/moonshine.dll').Hash -ne $moonInfo.sha256){throw 'Moonshine runtime identity mismatch'}
    $catalogText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'resources/caption-models.json')).TrimEnd()
    if(-not ([IO.File]::ReadAllText((Join-Path $PSScriptRoot 'src/caption_catalog.inc')).Contains($catalogText))){throw 'Caption catalog and compiled identities differ'}
    $sources = @('src/common.cpp','src/gfx.cpp','src/player.cpp','src/mpv_backend.cpp','src/ffmpeg.cpp','src/video.cpp','src/subtitle.cpp','src/captions.cpp','src/caption_models.cpp','src/caption_translation.cpp','src/model_dialog.cpp','src/playlist.cpp','src/media_source.cpp')+$nanoSources
    $cCompiler = Join-Path (Split-Path $compiler) 'gcc.exe'
    $videoIncludes = @('-Ithird_party/ffmpeg/include','-Ithird_party/libplacebo/mingw64/include')
    & $cCompiler '-std=c11' '-O2' '-ffunction-sections' '-fdata-sections' @videoIncludes '-c' 'src/placebo_av.c' '-o' "$outputDir/placebo_av.o"
    if ($LASTEXITCODE -ne 0) { throw 'Video frame helper build failed' }
    $videoDll = Join-Path $outputDir 'LumenVideo.dll'
    & $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Wno-deprecated-declarations' '-Wno-unused-parameter' '-Wno-missing-field-initializers' '-static' '-shared' '-s' '-Wl,--exclude-all-symbols' '-ffunction-sections' '-fdata-sections' '-Wl,--gc-sections' '-Wl,--no-insert-timestamp' @videoIncludes 'src/video_bridge.cpp' "$outputDir/placebo_av.o" 'third_party/libplacebo/mingw64/lib/libplacebo.dll.a' 'third_party/ffmpeg/lib/libavutil.dll.a' '-ld3d11' '-ldxgi' '-ldxguid' '-lole32' '-luser32' '-luuid' '-o' ($videoDll+'.building')
    if ($LASTEXITCODE -ne 0) { throw 'GPU renderer build failed' }
    Assert-UcrtImports ($videoDll+'.building') $reader
    Copy-BundleAsset ($videoDll+'.building') $videoDll
    Remove-Item -LiteralPath ($videoDll+'.building')
    $resourceCompiler = Join-Path (Split-Path $compiler) 'windres.exe'
    & $resourceCompiler '-I' 'resources' 'resources/LumenPlayer.rc' '-O' 'coff' '-o' "$outputDir/resources.o"
    if ($LASTEXITCODE -ne 0) { throw 'Resource build failed' }
    $sources += "$outputDir/resources.o"
    $flags = @('-std=c++17','-O2','-Wall','-Wextra','-Wno-missing-field-initializers',
        '-Wno-unused-parameter','-municode','-static','-ffunction-sections','-fdata-sections',
        '-Wl,--gc-sections','-Wl,--no-insert-timestamp','-Ithird_party/sherpa-onnx/include','-Ithird_party/ffmpeg/include')
    if($Target -in @('CaptionTest','CaptionRecoveryTest','OptimizationTest','NanoCaptionTest')){
        $sources += if($Target -eq 'CaptionRecoveryTest'){'test/caption_recovery.cpp'}elseif($Target -eq 'OptimizationTest'){'test/optimization_regression.cpp'}elseif($Target -eq 'NanoCaptionTest'){'test/nanosakura_playback.cpp'}else{'test/caption_manager_integration.cpp'}
        $output = Join-Path $outputDir $(if($Target -eq 'OptimizationTest'){'OptimizationRegression.exe'}elseif($Target -eq 'NanoCaptionTest'){'NanoCaptionTests.exe'}else{'CaptionIntegration.exe'})
        $flags += @('-mconsole','-DLUMEN_TEST','-Wno-unused-function')
        if($Target -eq 'CaptionRecoveryTest'){$flags += '-O0'}
    } elseif ($Target -eq 'CoreTest') {
        $sources += 'test/core_regression.cpp'
        $output = Join-Path $outputDir 'LumenCoreRegression.exe'
        $flags += @('-mconsole','-DLUMEN_TEST','-DLUMEN_CORE_TEST','-O0')
    } elseif ($Target -eq 'Test') {
        $sources += 'test/regression.cpp'
        $output = Join-Path $outputDir 'LumenRegression.exe'
        $flags += '-mconsole'
        $flags += '-DLUMEN_TEST'
    } else {
        $sources += 'src/main.cpp'
        $output = Join-Path $outputDir 'LumenPlayer.exe'
        $flags += @('-mwindows','-s')
    }
    $libs = @('-lmfplay','-lmfreadwrite','-lmfplat','-lmfuuid','-lmf','-lole32','-loleaut32',
        '-luuid','-lshlwapi','-lcomdlg32','-lshell32','-lgdi32','-ld2d1','-ldwrite',
        '-ldwmapi','-luser32','-ladvapi32','-lpropsys','-lwinmm','-lpsapi','-lwinhttp','-lbcrypt','-lws2_32','-lcomctl32','-luxtheme')
    & $compiler @flags @sources @libs @nanoLibraries @archiveLibraries '-o' ($output+'.building')
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
    Assert-UcrtImports ($output+'.building') $reader
    Copy-BundleAsset ($output+'.building') $output
    Remove-Item -LiteralPath ($output+'.building')
    # Build a runnable folder, including exactly the native CPU runtime used by
    # the tests. Copying these files never touches the user's INI or log.
    $bundle = @('third_party/sherpa-onnx/runtime/onnxruntime.dll',
        'third_party/sherpa-onnx/runtime/onnxruntime_providers_shared.dll',
        'third_party/sherpa-onnx/runtime/sherpa-onnx-c-api.dll')
    $bundle += 'resources/caption-cpu.conf'
    $bundle += @('resources/caption-models.json','third_party/moonshine/runtime/moonshine.dll')
    if($Target -ne 'Release'){$bundle+=Get-ChildItem -LiteralPath (Split-Path $fastModel) -File | ForEach-Object {$_.FullName}}
    $bundle += Get-ChildItem -LiteralPath 'third_party/sherpa-onnx/licenses' -File | ForEach-Object { $_.FullName }
    $bundle += Get-ChildItem -LiteralPath 'third_party/ffmpeg/bin' -Filter '*.dll' -File | ForEach-Object { $_.FullName }
    $bundle += Get-ChildItem -LiteralPath 'third_party/ffmpeg/licenses' -File | ForEach-Object { $_.FullName }
    $placeboInfo=Get-Content -LiteralPath 'third_party/libplacebo/build-provenance.json' -Raw -Encoding UTF8 | ConvertFrom-Json
    if((Get-FileHash -LiteralPath 'third_party/libplacebo/runtime/libplacebo-377.dll').Hash -ne $placeboInfo.dllSha256) {throw 'Renderer artifact verification failed'}
    $bundle += 'third_party/libplacebo/runtime/libplacebo-377.dll'
    # Shader and compiler runtime DLLs come from the verified UCRT64 core
    # closure below. Never stage a different CRT family before overwriting it.
    $bundle += $videoDll
    if(-not(Test-Path -LiteralPath (Join-Path $coreRuntime 'libmpv-2.dll'))) {throw 'Build the verified playback core first: python test/build_mpv_core.py'}
    $bundle += Get-ChildItem -LiteralPath $coreRuntime -Filter '*.dll' -File | ForEach-Object {$_.FullName}
    $destinations = @((Split-Path $output))
    if ($Install -and $Target -eq 'Release') {
        $destinations += (Join-Path $PSScriptRoot 'dist')
        Copy-BundleAsset $output (Join-Path $PSScriptRoot 'dist\LumenPlayer.exe')
    }
    foreach ($destination in $destinations) {
        $captionLicense=Join-Path $destination 'licenses/captions'
        New-Item -ItemType Directory -Path $captionLicense -Force | Out-Null
        Copy-Item -LiteralPath 'third_party/moonshine/licenses/LICENSE' -Destination (Join-Path $captionLicense 'Moonshine-LICENSE.txt') -Force
        Copy-Item -LiteralPath 'third_party/moonshine/provenance.json' -Destination (Join-Path $captionLicense 'Moonshine-provenance.json') -Force
        Copy-Item -LiteralPath 'third_party/nlohmann/LICENSE.MIT' -Destination (Join-Path $captionLicense 'JSON-LICENSE.MIT') -Force
        Copy-Item -LiteralPath 'third_party/nanosakura/MODEL-CARD.md' -Destination (Join-Path $captionLicense 'NanoSakura-MODEL-CARD.md') -Force
        Copy-Item -LiteralPath 'third_party/nanosakura/model-provenance.json' -Destination (Join-Path $captionLicense 'NanoSakura-model-provenance.json') -Force
        if(Test-Path -LiteralPath 'third_party/llama/LICENSE'){Copy-Item -LiteralPath 'third_party/llama/LICENSE' -Destination (Join-Path $captionLicense 'llama-LICENSE.txt') -Force}
        $licenseDestination = Join-Path $destination 'licenses/renderer'
        New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
        Copy-Item -LiteralPath 'third_party/libplacebo/mingw64/share/licenses' -Destination $licenseDestination -Recurse -Force
        $floatLicense = Join-Path $licenseDestination 'fast_float'
        New-Item -ItemType Directory -Path $floatLicense -Force | Out-Null
        Copy-Item -LiteralPath 'third_party/libplacebo/fast_float-8.3.1/LICENSE-MIT' -Destination $floatLicense -Force
        Copy-Item -LiteralPath 'resources/shaders' -Destination $destination -Recurse -Force
        $coreLicense=Join-Path $destination 'licenses/playback-core'
        New-Item -ItemType Directory -Path $coreLicense -Force | Out-Null
        Copy-Item -LiteralPath 'third_party/mpv/mpv-6a262d11f6/Copyright' -Destination $coreLicense -Force
        Copy-Item -LiteralPath 'third_party/mpv/mpv-6a262d11f6/LICENSE.LGPL' -Destination $coreLicense -Force
        if(Test-Path -LiteralPath 'third_party/mpv/licenses') {Copy-Item -LiteralPath 'third_party/mpv/licenses' -Destination $coreLicense -Recurse -Force}
        Copy-Item -LiteralPath 'third_party/mpv/deps-ucrt64/ucrt64/share/licenses' -Destination $coreLicense -Recurse -Force
        Copy-Item -LiteralPath 'third_party/mpv/dependency-lock.json' -Destination $coreLicense -Force
        Copy-Item -LiteralPath 'third_party/mpv/build-tools-requirements.txt' -Destination $coreLicense -Force
        Copy-Item -LiteralPath 'third_party/mpv/BUILDING.md' -Destination $coreLicense -Force
        foreach($record in @('build-provenance.json','provenance.json')) {
            $from=Join-Path 'third_party/mpv' $record
            if(Test-Path -LiteralPath $from){Copy-Item -LiteralPath $from -Destination $coreLicense -Force}
        }
        Copy-Item -LiteralPath 'test/mpv_pcm_tap.patch' -Destination $coreLicense -Force
        Copy-Item -LiteralPath 'test/build_mpv_core.py','test/prepare_mpv_core.py' -Destination $coreLicense -Force
        foreach ($file in $bundle) {
            $source = Get-Item -LiteralPath $file
            $folder = $destination
            if ($source.Directory.Name -eq $modelName) { $folder = Join-Path $destination $modelName }
            elseif ($source.Directory.Name -eq 'licenses') { $folder = Join-Path $destination 'licenses' }
            New-Item -ItemType Directory -Path $folder -Force | Out-Null
            $targetFile = Join-Path $folder $source.Name
            if ($source.FullName -eq $targetFile) { continue }
            if (-not (Test-Path -LiteralPath $targetFile) -or
                (Get-FileHash -LiteralPath $source.FullName).Hash -ne (Get-FileHash -LiteralPath $targetFile).Hash) {
                Copy-BundleAsset $source.FullName $targetFile
            }
        }
        # Models are managed by the user. Builds never replace or remove them.
        # Remove only the previously shipped player bridge, after a release
        # replacement is built. No browser profile or model path is consulted.
        $oldBridge = Join-Path $destination 'sodabridge.dll'
        if ($Target -eq 'Release' -and (Test-Path -LiteralPath $oldBridge)) {
            $bridgeItem = Get-Item -LiteralPath $oldBridge
            if (-not ($bridgeItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
                (Get-FileHash -LiteralPath $oldBridge).Hash -eq 'BBA63757374D79DC62E9D9F99711A72CD68F3DD9C7DA6FC024110029E861CF4E') {
                Remove-Item -LiteralPath $oldBridge
            }
        }
    }
    if ($Install -and $Target -eq 'Release') {
        & python 'test/compact_runtime.py' --directory (Join-Path $PSScriptRoot 'dist')
        if ($LASTEXITCODE -ne 0) { throw 'Installed runtime storage verification failed' }
    }
    Get-Item -LiteralPath $output | Select-Object FullName,Length
} finally { $env:TEMP=$previousTemp; $env:TMP=$previousTmp; Pop-Location }
