# Run from the developer terminal matching the installed Qt 5.14.2 kit.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QtKit,
    [ValidateSet('mingw32-make', 'nmake')][string]$MakeTool = 'mingw32-make',
    [string]$CacheDir = "$env:LOCALAPPDATA\mqtt-pc-pi"
)
$ErrorActionPreference = 'Stop'
$qmake = Join-Path $QtKit 'bin\qmake.exe'
if (-not (Test-Path $qmake)) { throw "Khong tim thay qmake: $qmake" }
$version = (& $qmake -query QT_VERSION).Trim()
if ($LASTEXITCODE -ne 0 -or $version -ne '5.14.2') { throw "Can Qt kit 5.14.2, hien tai: $version" }
Get-Command git -ErrorAction Stop | Out-Null
Get-Command $MakeTool -ErrorAction Stop | Out-Null
$perl = Get-Command perl -CommandType Application -ErrorAction SilentlyContinue
if (-not $perl) {
    throw 'Can Perl de chay Qt syncqt.pl. Cai Strawberry Perl 64-bit tu https://strawberryperl.com/, them Perl vao PATH va mo lai terminal MSVC.'
}
& $perl.Source -e 'exit 0'
if ($LASTEXITCODE -ne 0) { throw 'Perl khong chay duoc. Kiem tra cai dat Strawberry Perl va PATH bang perl -v.' }
$sourceDir = Join-Path $CacheDir "qtmqtt-$version"
$buildDir = Join-Path $sourceDir 'build'
if (-not (Test-Path $sourceDir)) {
    New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
    & git clone --depth 1 --branch "v$version" https://github.com/qt/qtmqtt.git $sourceDir
    if ($LASTEXITCODE -ne 0) { throw 'git clone failed' }
}
$tag = (& git -C $sourceDir describe --tags --exact-match HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $tag -ne "v$version") { throw 'Qt MQTT tag does not match the Qt kit' }
New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
Push-Location $buildDir
try {
    & $qmake (Join-Path $sourceDir 'qtmqtt.pro') 'QT_BUILD_PARTS=libs' 'CONFIG+=debug_and_release' 'CONFIG+=build_all'
    if ($LASTEXITCODE -ne 0) { throw 'qmake failed' }
    if ($MakeTool -eq 'mingw32-make') { & $MakeTool '-j2' } else { & $MakeTool }
    if ($LASTEXITCODE -ne 0) { throw 'Qt MQTT build failed' }
    & $MakeTool install
    if ($LASTEXITCODE -ne 0) { throw 'Qt MQTT install failed (check Qt kit directory permissions)' }
} finally {
    Pop-Location
}
Write-Output "Qt MQTT $version installed into $QtKit. Reconfigure the PC project in Qt Creator."
