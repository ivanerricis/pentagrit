# SPDX-License-Identifier: AGPL-3.0-or-later
# Downloads pluginval (once) and validates a VST3 plugin.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Vst3,
    [int]$Strictness = 10,
    [string]$ToolDir = (Join-Path $PSScriptRoot '..\..\build\tools\pluginval')
)

$ErrorActionPreference = 'Stop'
$exe = Join-Path $ToolDir 'pluginval.exe'

if (-not (Test-Path $exe)) {
    New-Item -ItemType Directory -Force $ToolDir | Out-Null
    $zip = Join-Path $ToolDir 'pluginval.zip'
    Write-Host 'Downloading pluginval...'
    Invoke-WebRequest 'https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip' -OutFile $zip
    Expand-Archive $zip -DestinationPath $ToolDir -Force
    Remove-Item $zip
}

if (-not (Test-Path $Vst3)) { throw "VST3 not found: $Vst3" }
& $exe --strictness-level $Strictness --validate-in-process --output-dir (Join-Path $ToolDir 'logs') $Vst3
exit $LASTEXITCODE
