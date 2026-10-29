# Shared by source staging and release packaging. Keep UE 5.8 reflected source
# outside the base plugin's Source directory for older-engine compatibility.
function Add-GeneralBlueprintMcp {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$PluginRoot,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $mcpDescriptor = Get-Content -LiteralPath (Join-Path $PluginRoot 'MCP\GeneralBlueprintMCP.uplugin') -Raw | ConvertFrom-Json
    $descriptorPath = Join-Path $Destination 'GeneralBlueprint.uplugin'
    $descriptor = Get-Content -LiteralPath $descriptorPath -Raw | ConvertFrom-Json
    foreach ($module in $mcpDescriptor.Modules) {
        $sourceModule = Join-Path $PluginRoot "MCP\Source\$($module.Name)"
        if (-not (Test-Path -LiteralPath (Join-Path $sourceModule "$($module.Name).Build.cs"))) {
            throw "MCP module source is missing: $sourceModule"
        }
        $stagedModule = Join-Path $Destination "Source\$($module.Name)"
        New-Item -ItemType Directory -Path $stagedModule -Force | Out-Null
        & robocopy $sourceModule $stagedModule /E /NFL /NDL /NJH /NJS /NP | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Failed to stage MCP module $($module.Name) (robocopy $LASTEXITCODE)." }
        $descriptor.Modules = @($descriptor.Modules | Where-Object Name -ne $module.Name) + @($module)
    }
    foreach ($dependency in @($mcpDescriptor.Plugins | Where-Object Name -ne 'GeneralBlueprint')) {
        $descriptor.Plugins = @($descriptor.Plugins | Where-Object Name -ne $dependency.Name) + @($dependency)
    }
    [IO.File]::WriteAllText($descriptorPath, (ConvertTo-Json -InputObject $descriptor -Depth 20), [Text.UTF8Encoding]::new($false))
}
