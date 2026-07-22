$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vcVars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$outputDirectory = Join-Path $env:TEMP 'hikari-physics-definition-scale-smoke'
$outputPath = Join-Path $outputDirectory 'HIKARI_PhysicsDefinitionScaleSmoke.exe'

if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "Visual Studio developer environment not found: $vcVars"
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sources = @(
    'Tools\Tests\HIKARI_PhysicsDefinitionScaleSmoke.cpp',
    'HIKARI\Render3D\HIKARI_Math3D.cpp',
    'HIKARI\Physics\HIKARI_PhysicsDefinitionScale.cpp'
) | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }

$includeEngine = Join-Path $repoRoot 'HIKARI'
$compile = @(
    'cl /nologo /std:c++20 /EHsc /MDd /utf-8',
    ('/I"{0}"' -f $includeEngine),
    ($sources -join ' '),
    ('/Fe:"{0}"' -f $outputPath)
) -join ' '
$command = 'call "{0}" && {1}' -f $vcVars, $compile

Push-Location $outputDirectory
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Physics definition scale smoke compilation failed with exit code $LASTEXITCODE"
    }
    & $outputPath
    if ($LASTEXITCODE -ne 0) {
        throw "Physics definition scale smoke failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host 'Physics definition scale smoke test passed.'
