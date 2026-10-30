param(
  [string]$Environment = 'x4pro_gh_release'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'
$repoRoot = Split-Path -Parent $PSScriptRoot
$iniText = Get-Content -Raw (Join-Path $repoRoot 'platformio.ini')
$tagMatch = [regex]::Match($iniText, '(?m)^web_release_tag\s*=\s*"([^"]+)"\s*$')
if (-not $tagMatch.Success) { throw 'web_release_tag is missing from [crosspoint] in platformio.ini.' }
$releaseTag = $tagMatch.Groups[1].Value

Push-Location $repoRoot
try {
  & py -3.13 -m platformio run -e $Environment
  if ($LASTEXITCODE -ne 0) { throw "PlatformIO build failed with exit code $LASTEXITCODE." }

  $buildDir = Join-Path $repoRoot ".pio\build\$Environment"
  $firmware = Join-Path $buildDir 'firmware.bin'
  if (-not (Test-Path -LiteralPath $firmware -PathType Leaf)) { throw "Firmware image not found: $firmware" }

  $bundles = @(Get-ChildItem -LiteralPath $buildDir -Filter 'web-*.bin' -File)
  if ($bundles.Count -ne 1) { throw "Expected exactly one generated web bundle in $buildDir, found $($bundles.Count)." }

  $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
  $outDir = Join-Path $repoRoot "artifacts\review\$releaseTag-$stamp"
  if (Test-Path -LiteralPath $outDir) { throw "Refusing to overwrite an existing review package: $outDir" }
  New-Item -ItemType Directory -Path $outDir | Out-Null
  $firmwareName = 'X4Libris-X4Pro-v1.0.bin'
  Copy-Item -LiteralPath $firmware -Destination (Join-Path $outDir $firmwareName)
  Copy-Item -LiteralPath $firmware -Destination (Join-Path $outDir 'firmware-x4pro.bin')
  Copy-Item -LiteralPath $bundles[0].FullName -Destination (Join-Path $outDir $bundles[0].Name)

  $manifestPath = Join-Path $outDir 'SHA256SUMS.txt'
  $lines = foreach ($file in Get-ChildItem -LiteralPath $outDir -File | Where-Object Name -ne 'SHA256SUMS.txt' | Sort-Object Name) {
    $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    "$hash  $($file.Name)"
  }
  [System.IO.File]::WriteAllLines($manifestPath, $lines, [System.Text.UTF8Encoding]::new($false))
  Write-Host "Review assets are in $outDir"
  Write-Host "Release tag configured for the web installer: $releaseTag"
} finally {
  Pop-Location
}
