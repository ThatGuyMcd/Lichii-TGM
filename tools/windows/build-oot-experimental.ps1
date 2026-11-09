param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [Parameter(Mandatory=$true)][string]$GlslangValidator,
    [string]$BuildDirectory,
    [int]$Jobs = 4,
    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'
$sourceDirectory = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $sourceDirectory 'build/oot-windows-x64'
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$QtRoot = (Resolve-Path -LiteralPath $QtRoot).Path
$GlslangValidator = (Resolve-Path -LiteralPath $GlslangValidator).Path
$null = New-Item -ItemType Directory -Force -Path (Join-Path $BuildDirectory 'tmp')
$env:TEMP = Join-Path $BuildDirectory 'tmp'
$env:TMP = $env:TEMP
$env:PATH = (Join-Path $QtRoot 'bin') + ';' + $env:PATH
. (Join-Path $PSScriptRoot 'load-msvc-env.ps1')
$llvm = Join-Path $env:ProgramFiles 'LLVM/bin'
$tests = if ($SkipTests) { 'OFF' } else { 'ON' }
$configureArgs = @(
    '-S', $sourceDirectory, '-B', $BuildDirectory, '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_C_COMPILER=$llvm/clang-cl.exe",
    "-DCMAKE_CXX_COMPILER=$llvm/clang-cl.exe",
    "-DCMAKE_LINKER=$llvm/lld-link.exe",
    "-DCMAKE_AR=$llvm/llvm-lib.exe",
    '-DCMAKE_C_FLAGS=-DWIN32 -D_WINDOWS -DNOMINMAX',
    '-DCMAKE_CXX_FLAGS=-DWIN32 -D_WINDOWS -EHsc -DNOMINMAX',
    "-DCMAKE_PREFIX_PATH=$QtRoot", "-DGLSLANGVALIDATOR=$GlslangValidator",
    "-DBUILD_TESTING=$tests", "-DYUZU_TESTS=$tests", "-DYUZU_QT_TESTS=$tests",
    '-DYUZU_CMD=ON', '-DYUZU_ROOM=OFF', '-DYUZU_ROOM_STANDALONE=OFF',
    '-DYUZU_USE_BUNDLED_QT=OFF', '-DYUZU_STATIC_BUILD=OFF',
    '-DYUZU_USE_BUNDLED_SDL3=ON', '-DUSE_DISCORD_PRESENCE=OFF',
    '-DENABLE_UPDATE_CHECKER=OFF', '-DUSE_CCACHE=OFF',
    '-DVulkanHeaders_FORCE_BUNDLED=ON', '-DVulkanUtilityLibraries_FORCE_BUNDLED=ON'
)
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $BuildDirectory --parallel $Jobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $SkipTests) {
    & ctest --test-dir $BuildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
