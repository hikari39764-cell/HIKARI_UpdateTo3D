$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vcVars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$outputDirectory = Join-Path $env:TEMP 'hikari-character-jump-input-smoke'
$outputPath = Join-Path $outputDirectory 'HIKARI_CharacterJumpInputSmoke.exe'

if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "Visual Studio developer environment not found: $vcVars"
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sourceFiles = @(
    'Tools\Tests\HIKARI_CharacterJumpInputSmoke.cpp',
    'HIKARI\Core\HIKARI_Logger.cpp',
    'HIKARI\Gameplay\Motion\HIKARI_MotionIntentService.cpp',
    'HIKARI\Input\Assets\HIKARI_InputActionMap.cpp',
    'HIKARI\Input\Assets\HIKARI_InputActionMapJson.cpp',
    'HIKARI\Input\Platform\HIKARI_Win32InputBackend.cpp',
    'HIKARI\Input\Platform\HIKARI_Win32InputCapture.cpp',
    'HIKARI\Input\Runtime\HIKARI_InputContextStack.cpp',
    'HIKARI\Input\Runtime\HIKARI_InputRebindOperation.cpp',
    'HIKARI\Input\Runtime\HIKARI_InputService.cpp',
    'HIKARI\Input\Runtime\HIKARI_InputTypes.cpp'
)
$sources = $sourceFiles | ForEach-Object {
    '"' + (Join-Path $repoRoot $_) + '"'
}

$compile = @(
    'cl /nologo /std:c++20 /EHsc /MDd /utf-8',
    ('/I"{0}"' -f (Join-Path $repoRoot 'HIKARI')),
    ('/I"{0}"' -f (Join-Path $repoRoot 'ThirdParty\nlohmann')),
    ($sources -join ' '),
    ('/Fe:"{0}"' -f $outputPath),
    '/link user32.lib'
) -join ' '
$command = 'call "{0}" && {1}' -f $vcVars, $compile

Push-Location $outputDirectory
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Character jump input smoke compilation failed with exit code $LASTEXITCODE"
    }
    & $outputPath $repoRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Character jump input smoke failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host 'Character jump input smoke test passed.'
