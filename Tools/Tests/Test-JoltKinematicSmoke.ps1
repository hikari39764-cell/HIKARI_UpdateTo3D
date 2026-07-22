$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$vsRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
$vcVars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$outputDirectory = Join-Path $env:TEMP 'hikari-jolt-kinematic-smoke'
$outputPath = Join-Path $outputDirectory 'HIKARI_JoltKinematicSmoke.exe'

if (-not (Test-Path -LiteralPath $vcVars)) {
    throw "Visual Studio developer environment not found: $vcVars"
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$sources = @(
    'Tools\Tests\HIKARI_JoltKinematicSmoke.cpp',
    'HIKARI\Assets\Collision\HIKARI_CollisionGeometryAsset.cpp',
    'HIKARI\Assets\Collision\HIKARI_HcollisionFormat.cpp',
    'HIKARI\Render3D\HIKARI_Math3D.cpp',
    'HIKARI\Physics\HIKARI_PhysicsBodyValidator.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltPhysicsBackend.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltCharacters.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltCharacterSimulation.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltContacts.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltQueries.cpp',
    'HIKARI\Physics\Backends\Jolt\HIKARI_JoltShapeFactory.cpp'
) | ForEach-Object { '"' + (Join-Path $repoRoot $_) + '"' }

$includeEngine = Join-Path $repoRoot 'HIKARI'
$includeJolt = Join-Path $repoRoot 'vcpkg_installed\x64-windows\include'
$joltLibrary = Join-Path $repoRoot 'vcpkg_installed\x64-windows\debug\lib\Jolt.lib'
$sceneCollision = Join-Path $repoRoot 'Library\Imported\316766690d9a63d68e3eff3494736c27\collision_geometry.hcollision'
$compile = @(
    'cl /nologo /std:c++20 /EHsc /MDd /utf-8',
    '/DNOMINMAX /DWIN32_LEAN_AND_MEAN /DJPH_FLOATING_POINT_EXCEPTIONS_ENABLED /DJPH_USE_CPU_COMPUTE /DJPH_OBJECT_STREAM',
    ('/I"{0}"' -f $includeEngine),
    ('/I"{0}"' -f $includeJolt),
    ($sources -join ' '),
    ('/Fe:"{0}"' -f $outputPath),
    ('/link "{0}"' -f $joltLibrary)
) -join ' '
$command = 'call "{0}" && {1}' -f $vcVars, $compile

Push-Location $outputDirectory
try {
    & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Kinematic smoke-test compilation failed with exit code $LASTEXITCODE"
    }
    & $outputPath $sceneCollision
    if ($LASTEXITCODE -ne 0) {
        throw "Kinematic smoke test failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

Write-Host 'Jolt kinematic smoke test passed.'
