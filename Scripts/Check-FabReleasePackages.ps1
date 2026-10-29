[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ManifestPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

$manifest = @(Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json)
if ($manifest.Count -eq 0) { throw "Manifest has no packages: $ManifestPath" }

$failed = $false
foreach ($entry in $manifest) {
    $issues = [System.Collections.Generic.List[string]]::new()
    $zipPath = [string]$entry.Package
    if ($entry.Status -ne 'packaged' -or -not (Test-Path -LiteralPath $zipPath -PathType Leaf)) {
        Write-Error "UE $($entry.Engine): package missing or build not marked packaged"
        $failed = $true
        continue
    }

    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne [string]$entry.Sha256) { $issues.Add('SHA-256 does not match the manifest') }
    $sidecar = "$zipPath.sha256"
    if (-not (Test-Path -LiteralPath $sidecar -PathType Leaf)) {
        $issues.Add('SHA-256 sidecar is missing')
    }
    elseif ((Get-Content -LiteralPath $sidecar -Raw).Trim() -ne "$actualHash  $(Split-Path -Leaf $zipPath)") {
        $issues.Add('SHA-256 sidecar does not match the archive')
    }

    $zip = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $names = @($zip.Entries | ForEach-Object FullName)
        $descriptorName = 'GeneralBlueprint/GeneralBlueprint.uplugin'
        foreach ($required in @(
            $descriptorName,
            'GeneralBlueprint/Config/DefaultEditor.ini',
            'GeneralBlueprint/Resources/Icon128.png',
            'GeneralBlueprint/README.md',
            'GeneralBlueprint/Docs/Compatibility.md',
            'GeneralBlueprint/Docs/UserGuide.md',
            'GeneralBlueprint/Docs/UserGuide.txt',
            'GeneralBlueprint/Docs/UserGuide.zh-CN.md'
        )) {
            if ($names -notcontains $required) { $issues.Add("Missing $required") }
        }
        foreach ($directory in @('Source', 'Content', 'Config', 'Resources')) {
            if (-not @($names | Where-Object { $_.StartsWith("GeneralBlueprint/$directory/", [StringComparison]::Ordinal) }).Count) {
                $issues.Add("Missing $directory directory")
            }
        }
        $uplugins = @($names | Where-Object { $_.EndsWith('.uplugin', [StringComparison]::OrdinalIgnoreCase) })
        if ($uplugins.Count -ne 1 -or $uplugins[0] -ne $descriptorName) {
            $issues.Add("Expected exactly one root plugin descriptor, found $($uplugins.Count)")
        }
        if (@($names | Where-Object { -not $_.StartsWith('GeneralBlueprint/', [StringComparison]::Ordinal) }).Count) {
            $issues.Add('Archive contains files outside its single plugin root')
        }
        if (@($names | Where-Object { $_ -match '^GeneralBlueprint/(Docs/Fab|Dev|Releases|\.buildwork)/' }).Count) {
            $issues.Add('Archive contains internal release or development files')
        }

        foreach ($sourceEntry in @($zip.Entries | Where-Object {
            $_.FullName.StartsWith('GeneralBlueprint/Source/', [StringComparison]::Ordinal) -and
            $_.FullName -match '\.(h|cpp|cs)$'
        })) {
            $reader = [System.IO.StreamReader]::new($sourceEntry.Open())
            try { $sourceText = $reader.ReadToEnd() }
            finally { $reader.Dispose() }
            if ($sourceText -match 'Copyright\s*\(c\)\s*2026\s+(OpenClaw Research|ccc887)') {
                $issues.Add("Outdated copyright attribution: $($sourceEntry.FullName)")
            }
        }

        $descriptorEntry = $zip.GetEntry($descriptorName)
        if ($descriptorEntry) {
            $reader = [System.IO.StreamReader]::new($descriptorEntry.Open())
            try { $descriptor = $reader.ReadToEnd() | ConvertFrom-Json }
            finally { $reader.Dispose() }
            if (-not ([string]$descriptor.EngineVersion).StartsWith("$($entry.Engine).", [StringComparison]::Ordinal)) {
                $issues.Add("Descriptor EngineVersion does not match UE $($entry.Engine)")
            }
            if ($descriptor.CreatedBy -ne 'cc8887') { $issues.Add('Publisher name does not match cc8887') }
            if ($descriptor.CreatedByURL -ne 'https://github.com/cc8887') { $issues.Add('Publisher URL is missing or incorrect') }
            if ($descriptor.SupportURL -ne 'https://github.com/cc8887') { $issues.Add('Support URL is missing or incorrect') }
            if ($descriptor.Installed -ne $true) { $issues.Add('Packaged descriptor is not marked installed') }
            $moduleNames = @($descriptor.Modules | ForEach-Object Name)
            foreach ($module in @('BlueprintLisp', 'AnimBP2FP', 'MatBP2FP', 'BlueprintAutoLayout')) {
                if ($moduleNames -notcontains $module) { $issues.Add("Missing module $module") }
            }
            foreach ($module in @('BlueprintLispMCP', 'AnimBP2FPMCP', 'MatBP2FPMCP', 'BlueprintAutoLayoutMCP', 'DrivenHighlightMCP')) {
                if (($entry.Engine -eq '5.8') -ne ($moduleNames -contains $module)) {
                    $issues.Add("Unexpected MCP module state: $module")
                }
            }
            $binaryPrefix = if ([string]$entry.Engine -like '4.*') { 'UE4Editor' } else { 'UnrealEditor' }
            foreach ($module in $moduleNames) {
                if ($names -notcontains "GeneralBlueprint/Source/$module/$module.Build.cs") {
                    $issues.Add("Missing module source rules: $module")
                }
                if ($names -notcontains "GeneralBlueprint/Binaries/Win64/$binaryPrefix-$module.dll") {
                    $issues.Add("Missing Win64 Editor binary: $module")
                }
            }
        }
    }
    finally { $zip.Dispose() }

    if ($issues.Count) {
        $failed = $true
        Write-Host "FAIL UE $($entry.Engine): $($issues -join '; ')" -ForegroundColor Red
    }
    else {
        Write-Host "PASS UE $($entry.Engine): structure, attribution, version, and SHA-256" -ForegroundColor Green
    }
}

if ($failed) { throw 'One or more Fab release packages failed validation.' }
