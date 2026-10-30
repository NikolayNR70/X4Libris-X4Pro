param(
    [ValidateSet('run', 'clean', 'test', 'upload')]
    [string] $Action = 'run',

    [string] $Environment = 'x4pro_gh_release',

    [switch] $VerboseBuild
)

$ErrorActionPreference = 'Stop'

Set-Location (Split-Path -Parent $PSScriptRoot)

# Keep PlatformIO/SCons and child Python scripts on UTF-8 so non-ASCII project
# strings (for example translated language names) cannot break console output.
& chcp.com 65001 | Out-Null
$env:PYTHONIOENCODING = 'utf-8'
$env:PYTHONUTF8 = '1'
[Console]::InputEncoding = [System.Text.UTF8Encoding]::new()
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()

$platformio = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\platformio.exe'
if (-not (Test-Path -LiteralPath $platformio)) {
    throw "PlatformIO executable not found: $platformio"
}

$arguments = @($Action, '-e', $Environment)
if ($VerboseBuild) { $arguments += '-v' }
& $platformio @arguments
exit $LASTEXITCODE
