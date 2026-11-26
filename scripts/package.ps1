# Builds a release package of Semplice.
#
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-Compiler mingw|msvc] [-SkipTests] [-SkipInstaller]
#
# Result (in dist\):
#   Semplice\                            the program folder (runs without Qt installed)
#   Semplice-<version>.zip               the same folder, portable
#   Semplice-symbols\                    debug information for crash reports (keep it, do not ship it)
#   Semplice-<version>-setup.exe         the installer, when Inno Setup 6 is installed
#
# Needs: CMake and Ninja, the Qt kit (default D:\Programs\Qt\6.11.2), MinGW (D:\Programs\Qt\Tools\mingw1310_64)
# or Visual Studio 2022. Change the three paths below if yours differ.
param(
    [ValidateSet('mingw', 'msvc')][string]$Compiler = 'mingw',
    [string]$QtRoot = 'D:\Programs\Qt',
    [string]$QtVersion = '6.11.2',
    [switch]$SkipTests,
    [switch]$SkipInstaller
)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $root

function Step($text) { Write-Host "`n=== $text" -ForegroundColor Cyan }
function Run($exe, [string[]]$arguments) {
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

# ---- environment
$kit = if ($Compiler -eq 'mingw') { 'mingw_64' } else { 'msvc2022_64' }
$qtBin = Join-Path $QtRoot "$QtVersion\$kit\bin"
$env:CMAKE_PREFIX_PATH = Join-Path $QtRoot "$QtVersion\$kit"
$env:PATH = "$(Join-Path $QtRoot 'Tools\Ninja');$qtBin;$env:PATH"
if ($Compiler -eq 'mingw') {
    $mingwBin = Join-Path $QtRoot 'Tools\mingw1310_64\bin'
    $env:PATH = "$mingwBin;$env:PATH"
} else {
    $vcvars = Get-ChildItem "${env:ProgramFiles}\Microsoft Visual Studio\2022\*\VC\Auxiliary\Build\vcvars64.bat" | Select-Object -First 1
    if (-not $vcvars) { throw 'Visual Studio 2022 (vcvars64.bat) not found' }
    cmd /c "`"$($vcvars.FullName)`" >nul && set" | ForEach-Object { if ($_ -match '^(.*?)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] } }
}
$preset = "windows-$Compiler-release"

# ---- version
$version = (Select-String -Path 'CMakeLists.txt' -Pattern 'project\(Semplice VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$name = "Semplice"   # the folders carry no version (the zip and the installer do)
$dist = Join-Path $root 'dist'
$folder = Join-Path $dist $name
$symbols = Join-Path $dist "$name-symbols"
Write-Host "Semplice $version, $Compiler"

# ---- build (with debug information, split off below)
Step 'Configure and build (Release with symbols)'
$flags = if ($Compiler -eq 'mingw') { '-O2 -g -DNDEBUG' } else { '/O2 /Zi /DNDEBUG' }
Run cmake @('--preset', $preset, "-DCMAKE_CXX_FLAGS_RELEASE=$flags")
Run cmake @('--build', '--preset', $preset)

if (-not $SkipTests) {
    Step 'Tests'
    Run ctest @('--preset', $preset, '--output-on-failure')
}

# ---- program folder
Step 'Program folder'
if (Test-Path $folder) { Remove-Item $folder -Recurse -Force }
New-Item -ItemType Directory -Force $folder | Out-Null
$build = Join-Path $root "build\$preset"
Copy-Item (Join-Path $build 'Semplice.exe') $folder
Run (Join-Path $qtBin 'windeployqt.exe') @('--release', '--no-translations', '--no-system-d3d-compiler', '--no-opengl-sw',
    '--qmldir', (Join-Path $root 'src\qml'), (Join-Path $folder 'Semplice.exe'))

# only the Basic control style is used: drop the other styles windeployqt copies because they exist
foreach ($style in 'Fusion', 'Imagine', 'Material', 'Universal', 'FluentWinUI3', 'WindowsStyle', 'Windows', 'NativeStyle') {
    Get-ChildItem $folder -Recurse -Force -Filter "*Controls2$style*" | Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    Get-ChildItem (Join-Path $folder 'qml\QtQuick\Controls') -Directory -Filter $style -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
}
Get-ChildItem (Join-Path $folder 'qml\QtQuick\NativeStyle') -ErrorAction SilentlyContinue | Remove-Item -Recurse -Force
Copy-Item 'THIRD_PARTY_NOTICES.md' $folder
Copy-Item 'resources\license\LICENSE.txt' (Join-Path $folder 'LICENSE.txt')
Copy-Item 'docs\BETA_GUIDE.md' $folder

# ---- symbols
Step 'Symbols'
if (Test-Path $symbols) { Remove-Item $symbols -Recurse -Force }
New-Item -ItemType Directory -Force $symbols | Out-Null
if ($Compiler -eq 'mingw') {
    $objcopy = Join-Path $mingwBin 'objcopy.exe'
    $strip = Join-Path $mingwBin 'strip.exe'
    Get-ChildItem $folder -Filter 'Semplice.exe' | ForEach-Object {
        & $objcopy --only-keep-debug $_.FullName (Join-Path $symbols ($_.Name + '.debug'))
        & $strip --strip-debug $_.FullName
        & $objcopy "--add-gnu-debuglink=$(Join-Path $symbols ($_.Name + '.debug'))" $_.FullName
    }
} else {
    Copy-Item (Join-Path $build 'Semplice.pdb') $symbols -ErrorAction SilentlyContinue
}

# ---- zip
Step 'Zip'
$zip = Join-Path $dist "Semplice-$version.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path $folder -DestinationPath $zip
Write-Host $zip

# ---- installer
if (-not $SkipInstaller) {
    Step 'Installer'
    $iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "${env:ProgramFiles}\Inno Setup 6\ISCC.exe", "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($iscc) {
        Run $iscc @("/DAppVersion=$version", "/DSourceDir=$folder", "/DOutputDir=$dist", (Join-Path $root 'installer\Semplice.iss'))
    } else {
        Write-Warning 'Inno Setup 6 was not found: the installer was NOT built (install it from jrsoftware.org, then run this script again).'
    }
}

Step 'Done'
Write-Host "Program folder: $folder"
Write-Host "Keep this folder for crash reports: $symbols"
