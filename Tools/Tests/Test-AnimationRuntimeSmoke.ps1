$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vcVars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$outputDirectory = Join-Path $env:TEMP 'hikari-animation-runtime-smoke'
$outputPath = Join-Path $outputDirectory 'HIKARI_AnimationRuntimeSmoke.exe'

if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "Visual Studio developer environment not found: $vcVars"
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sourceFiles = @(
    'Tools\Tests\HIKARI_AnimationRuntimeSmoke.cpp',
    'HIKARI\Animation\Runtime\HIKARI_AnimationClipSampler.cpp',
    'HIKARI\Animation\Runtime\HIKARI_AnimationPoseService.cpp',
    'HIKARI\Assets\Formats\HIKARI_HmodelFormat.cpp',
    'HIKARI\Render3D\Core\HIKARI_ModelAsset.cpp',
    'HIKARI\Render3D\HIKARI_Math3D.cpp'
)
$sources = $sourceFiles | ForEach-Object {
    '"' + (Join-Path $repoRoot $_) + '"'
}

$compile = @(
    'cl /nologo /std:c++20 /EHsc /MDd /utf-8',
    ('/I"{0}"' -f (Join-Path $repoRoot 'HIKARI')),
    ($sources -join ' '),
    ('/Fe:"{0}"' -f $outputPath)
) -join ' '
$command = 'call "{0}" && {1}' -f $vcVars, $compile

Push-Location $outputDirectory
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Animation runtime smoke compilation failed with exit code $LASTEXITCODE"
    }
    & $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "Animation runtime smoke failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host 'Animation runtime smoke test passed.'
