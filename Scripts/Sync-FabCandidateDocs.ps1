[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SourceManifest,

    [Parameter(Mandatory = $true)]
    [string]$DestinationRoot,

    [string]$PluginRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression

$SourceManifest = [IO.Path]::GetFullPath($SourceManifest)
$sourceRoot = [IO.Path]::GetFullPath((Split-Path -Parent $SourceManifest))
$DestinationRoot = [IO.Path]::GetFullPath($DestinationRoot)
$PluginRoot = [IO.Path]::GetFullPath($PluginRoot)
$docSource = Join-Path $PluginRoot 'Docs\Compatibility.md'
$docEntryName = 'GeneralBlueprint/Docs/Compatibility.md'

if (-not (Test-Path -LiteralPath $docSource -PathType Leaf)) { throw "Documentation source missing: $docSource" }
if (Test-Path -LiteralPath $DestinationRoot) { throw "Destination already exists: $DestinationRoot" }
if (-not $DestinationRoot.StartsWith(([IO.Path]::GetFullPath((Join-Path $PluginRoot 'Releases')) + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destination must be a new directory within the plugin's Releases directory: $DestinationRoot"
}

$items = @(Get-Content -LiteralPath $SourceManifest -Raw | ConvertFrom-Json)
if ($items.Count -ne 11) { throw "Expected 11 source packages; found $($items.Count)" }
New-Item -ItemType Directory -Path $DestinationRoot | Out-Null
$result = [System.Collections.Generic.List[object]]::new()

foreach ($item in $items) {
    if ($item.Status -ne 'packaged') { throw "UE $($item.Engine) is not packaged in source manifest" }
    $sourceZip = [IO.Path]::GetFullPath([string]$item.Package)
    if (-not $sourceZip.StartsWith(($sourceRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Source package is outside the source release: $sourceZip"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne [string]$item.Sha256) { throw "Source hash mismatch: $sourceZip" }

    $targetDirectory = Join-Path $DestinationRoot ("UE_" + $item.Engine)
    New-Item -ItemType Directory -Path $targetDirectory | Out-Null
    $targetZip = Join-Path $targetDirectory (Split-Path -Leaf $sourceZip)
    Copy-Item -LiteralPath $sourceZip -Destination $targetZip

    $before = @{}
    $sourceArchive = [System.IO.Compression.ZipFile]::OpenRead($sourceZip)
    try {
        foreach ($entry in $sourceArchive.Entries) {
            if ($entry.FullName -ne $docEntryName) {
                $before[$entry.FullName] = "$($entry.Length)|$($entry.Crc32)"
            }
        }
    }
    finally { $sourceArchive.Dispose() }

    $targetArchive = [System.IO.Compression.ZipFile]::Open($targetZip, [System.IO.Compression.ZipArchiveMode]::Update)
    try {
        $oldDoc = $targetArchive.GetEntry($docEntryName)
        if (-not $oldDoc) { throw "Missing document in $targetZip" }
        $oldDoc.Delete()
        $newDoc = $targetArchive.CreateEntry($docEntryName, [System.IO.Compression.CompressionLevel]::Optimal)
        $inputStream = [IO.File]::OpenRead($docSource)
        $outputStream = $newDoc.Open()
        try { $inputStream.CopyTo($outputStream) }
        finally { $outputStream.Dispose(); $inputStream.Dispose() }
    }
    finally { $targetArchive.Dispose() }

    $targetArchive = [System.IO.Compression.ZipFile]::OpenRead($targetZip)
    try {
        $after = @{}
        foreach ($entry in $targetArchive.Entries) {
            if ($entry.FullName -ne $docEntryName) {
                $after[$entry.FullName] = "$($entry.Length)|$($entry.Crc32)"
            }
        }
        if ($after.Count -ne $before.Count) { throw "Non-document ZIP entry count changed for UE $($item.Engine)" }
        foreach ($name in $before.Keys) {
            if (-not $after.ContainsKey($name) -or $after[$name] -ne $before[$name]) {
                throw "Non-document ZIP entry changed for UE $($item.Engine): $name"
            }
        }
        $docEntry = $targetArchive.GetEntry($docEntryName)
        $reader = [IO.StreamReader]::new($docEntry.Open())
        try { $text = $reader.ReadToEnd() }
        finally { $reader.Dispose() }
        if ($text -ne [IO.File]::ReadAllText($docSource)) { throw "Synchronized document content differs for UE $($item.Engine)" }
    }
    finally { $targetArchive.Dispose() }

    $hash = (Get-FileHash -LiteralPath $targetZip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$targetZip.sha256", "$hash  $(Split-Path -Leaf $targetZip)`n", [Text.UTF8Encoding]::new($false))
    $item.Package = $targetZip
    $item.Sha256 = $hash
    $item | Add-Member -NotePropertyName DocumentationSyncSourceSha256 -NotePropertyValue $sourceHash -Force
    $result.Add($item)
    Write-Host "UE $($item.Engine): synchronized Compatibility.md; all other ZIP entry sizes and CRCs unchanged"
}

$json = ConvertTo-Json -InputObject @($result.ToArray()) -Depth 8
[IO.File]::WriteAllText((Join-Path $DestinationRoot 'manifest.json'), $json, [Text.UTF8Encoding]::new($false))
if (Test-Path -LiteralPath (Join-Path $sourceRoot 'Media') -PathType Container) {
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'Media') -Destination $DestinationRoot -Recurse
}
$note = @"
# Documentation sync evidence

The release ZIPs were copied from ``$sourceRoot``. Only ``GeneralBlueprint/Docs/Compatibility.md`` was replaced from the maintained source document. For every engine package, all other ZIP entry names, uncompressed sizes, and CRC32 values were checked for equality before and after. The binaries and source code tested under the source candidate are unchanged. See ``$sourceRoot\QA\ValidationSummary.md`` and its reports for the test evidence. The manifest records the prior ZIP SHA-256 digest in ``DocumentationSyncSourceSha256`` and the new digest in ``Sha256``.
"@
[IO.File]::WriteAllText((Join-Path $DestinationRoot 'DocumentationSync.md'), $note, [Text.UTF8Encoding]::new($false))
Write-Host "Documentation-synced candidate: $DestinationRoot"
