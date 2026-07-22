$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vcVars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$outputDirectory = Join-Path $env:TEMP 'hikari-animation-state-machine-smoke'
$outputPath = Join-Path $outputDirectory 'HIKARI_AnimationStateMachineSmoke.exe'

if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "Visual Studio developer environment not found: $vcVars"
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sourceFiles = @(
    'Tools\Tests\HIKARI_AnimationStateMachineSmoke.cpp',
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachine.cpp',
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMotionEvaluator.cpp',
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachineJson.cpp',
    'HIKARI\Animation\StateMachine\HIKARI_AnimationStateMachineInstance.cpp',
    'HIKARI\Assets\Animation\HIKARI_AnimationStateMachineAsset.cpp'
)
$sources = $sourceFiles | ForEach-Object {
    '"' + (Join-Path $repoRoot $_) + '"'
}

$compile = @(
    'cl /nologo /std:c++20 /EHsc /MDd /utf-8',
    ('/I"{0}"' -f (Join-Path $repoRoot 'HIKARI')),
    ('/I"{0}"' -f (Join-Path $repoRoot 'ThirdParty\nlohmann')),
    ($sources -join ' '),
    ('/Fe:"{0}"' -f $outputPath)
) -join ' '
$command = 'call "{0}" && {1}' -f $vcVars, $compile

Push-Location $outputDirectory
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Animation state machine smoke compilation failed with exit code $LASTEXITCODE"
    }
    & $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "Animation state machine smoke failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host 'Animation state machine smoke test passed.'
