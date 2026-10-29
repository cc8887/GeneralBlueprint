[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$EngineRoot,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$PluginRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$PluginRoot = [IO.Path]::GetFullPath($PluginRoot)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$engineVersion = Get-Content -LiteralPath (Join-Path $EngineRoot 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
if ($engineVersion.MajorVersion -ne 5 -or $engineVersion.MinorVersion -ne 8) {
    throw 'This staging entrypoint targets UE 5.8.x only.'
}
if (Test-Path -LiteralPath $OutputDirectory) { throw "Choose a new output directory: $OutputDirectory" }
if ($PluginRoot.StartsWith($OutputDirectory.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must not contain the source checkout.'
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$excludedNames = @('.git', '.buildwork', 'Binaries', 'Intermediate', 'Saved', 'DerivedDataCache', 'Artifacts', 'Releases', 'Dev', 'MCP')
$excluded = @($excludedNames | ForEach-Object { Join-Path $PluginRoot $_ }) + @($OutputDirectory)
$copyArgs = @($PluginRoot, $OutputDirectory, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/XD') + $excluded + @('/XF', '.gitignore')
& robocopy @copyArgs | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Source staging failed (robocopy $LASTEXITCODE)." }
. (Join-Path $PSScriptRoot 'GeneralBlueprintMcp.ps1')
Add-GeneralBlueprintMcp -PluginRoot $PluginRoot -Destination $OutputDirectory
Write-Output "Staged UE 5.8 source plugin: $OutputDirectory"
