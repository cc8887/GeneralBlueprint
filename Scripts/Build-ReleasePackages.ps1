[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [hashtable]$EngineRoots,

    [string[]]$Versions = @('4.26', '4.27', '5.0', '5.1', '5.2', '5.3', '5.4', '5.5', '5.6', '5.7', '5.8'),

    [string]$PluginRoot = (Split-Path -Parent $PSScriptRoot),

    [string]$OutputRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Releases'),

    [switch]$KeepWork
)

$ErrorActionPreference = 'Stop'
$PluginRoot = [IO.Path]::GetFullPath($PluginRoot)
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$results = New-Object 'System.Collections.Generic.List[object]'
$allVersions = @('4.26', '4.27', '5.0', '5.1', '5.2', '5.3', '5.4', '5.5', '5.6', '5.7', '5.8')
. (Join-Path $PSScriptRoot 'GeneralBlueprintMcp.ps1')

if (-not (Test-Path -LiteralPath (Join-Path $PluginRoot 'GeneralBlueprint.uplugin'))) {
    throw "GeneralBlueprint.uplugin not found under $PluginRoot"
}

New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null
$logRoot = Join-Path $OutputRoot 'Logs'
New-Item -ItemType Directory -Path $logRoot -Force | Out-Null
$manifestPath = Join-Path $OutputRoot 'manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    foreach ($priorResult in @(Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)) {
        $results.Add($priorResult)
    }
}

function Save-Manifest {
    param([System.Collections.Generic.List[object]]$Items, [string]$Path)
    $json = ConvertTo-Json -InputObject @($Items.ToArray()) -Depth 8
    [IO.File]::WriteAllText($Path, $json, [Text.UTF8Encoding]::new($false))
}

function Remove-EngineResult {
    param([System.Collections.Generic.List[object]]$Items, [string]$Engine)
    $remaining = @($Items | Where-Object { [string]$_.Engine -ne $Engine })
    $Items.Clear()
    foreach ($item in $remaining) { $Items.Add($item) }
}

foreach ($version in $Versions) {
    Remove-EngineResult -Items $results -Engine $version
    if ($version -notin $allVersions) {
        $results.Add([pscustomobject]@{ Engine = $version; Status = 'invalid-version'; Package = ''; Sha256 = ''; Log = ''; Error = 'Version is outside the declared release range.' })
        Save-Manifest -Items $results -Path $manifestPath
        continue
    }

    $runStarted = Get-Date
    $workRoot = $null
    $versionLog = Join-Path $logRoot "Build-UE_$version.log"

    try {
        if (-not $EngineRoots.ContainsKey($version)) {
            throw "No engine root supplied for UE $version."
        }

        $engineRoot = [IO.Path]::GetFullPath([string]$EngineRoots[$version])
        $versionFile = Join-Path $engineRoot 'Engine\Build\Build.version'
        $uat = Join-Path $engineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
        if (-not (Test-Path -LiteralPath $versionFile)) { throw "Build.version not found: $versionFile" }
        if (-not (Test-Path -LiteralPath $uat)) { throw "RunUAT.bat not found: $uat" }

        $engineVersion = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
        $actualVersion = '{0}.{1}' -f $engineVersion.MajorVersion, $engineVersion.MinorVersion
        if ($actualVersion -ne $version) {
            throw "Engine root identifies as UE $actualVersion, not UE $version."
        }

        $versionRoot = Join-Path $OutputRoot "UE_$version"
        New-Item -ItemType Directory -Path $versionRoot -Force | Out-Null
        $builtEngineVersion = '{0}.{1}.{2}' -f $engineVersion.MajorVersion, $engineVersion.MinorVersion, $engineVersion.PatchVersion
        $zipPath = Join-Path $versionRoot "GeneralBlueprint-UE_$builtEngineVersion-Win64.zip"
        if (Test-Path -LiteralPath $zipPath) {
            throw "Package already exists; move it aside before rebuilding: $zipPath"
        }

        $workRoot = Join-Path $PluginRoot (".buildwork\UE_$version-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
        $stageRoot = Join-Path $workRoot 'Plugin'
        $packageRoot = Join-Path $workRoot 'Package\GeneralBlueprint'
        New-Item -ItemType Directory -Path $workRoot,$stageRoot | Out-Null

        $excluded = @(
            (Join-Path $PluginRoot '.git'),
            (Join-Path $PluginRoot 'Binaries'),
            (Join-Path $PluginRoot 'Intermediate'),
            (Join-Path $PluginRoot 'Saved'),
            (Join-Path $PluginRoot 'DerivedDataCache'),
            (Join-Path $PluginRoot 'Artifacts'),
            (Join-Path $PluginRoot 'Releases'),
            (Join-Path $PluginRoot 'Dev'),
            (Join-Path $PluginRoot '.buildwork'),
            (Join-Path $PluginRoot 'MCP')
        )
        $copyArgs = @($PluginRoot, $stageRoot, '/E', '/NFL', '/NDL', '/NJH', '/NJS', '/NP', '/XD') + $excluded + @('/XF', '.gitignore')
        & robocopy @copyArgs | Out-Null
        if ($LASTEXITCODE -ge 8) { throw "Plugin staging copy failed with robocopy exit code $LASTEXITCODE." }

        if ($version -eq '5.8') {
            Add-GeneralBlueprintMcp -PluginRoot $PluginRoot -Destination $stageRoot
        }

        # Fab accepts an English .txt guide. Generate it from the maintained
        # Markdown source for each staged package so the copies cannot drift.
        $guideMarkdown = Join-Path $stageRoot 'Docs\UserGuide.md'
        $guideText = Join-Path $stageRoot 'Docs\UserGuide.txt'
        if (-not (Test-Path -LiteralPath $guideMarkdown -PathType Leaf)) {
            throw "English user guide is missing from staging: $guideMarkdown"
        }
        Copy-Item -LiteralPath $guideMarkdown -Destination $guideText

        $previousRollForward = $env:DOTNET_ROLL_FORWARD
        $previousCompilerOptions = $env:CL
        if ($version -eq '5.0') { $env:DOTNET_ROLL_FORWARD = 'Major' }
        if ($version -in @('5.0', '5.1')) {
            # UE 5.0's engine shared PCH calls Clang's __has_feature under MSVC.
            # Some UE 5.1 toolchains need the same workaround. Force-include this
            # function-like macro only in the packaging process.
            $compatHeaderPath = Join-Path $workRoot 'ue5-msvc-has-feature-compat.h'
            $compatHeader = "#ifndef __has_feature`n#define __has_feature(x) 0`n#endif`n"
            [IO.File]::WriteAllText($compatHeaderPath, $compatHeader, [Text.Encoding]::ASCII)
            $env:CL = (($previousCompilerOptions + " /FI$compatHeaderPath").Trim())
        }
        try {
            $uatArgs = @(
                'BuildPlugin',
                "-Plugin=$(Join-Path $stageRoot 'GeneralBlueprint.uplugin')",
                "-Package=$packageRoot",
                '-TargetPlatforms=Win64',
                '-Rocket'
            )
            if ($engineVersion.MajorVersion -eq 4) { $uatArgs += '-VS2019' }
            & $uat @uatArgs 2>&1 |
                Tee-Object -FilePath $versionLog
            $uatExitCode = $LASTEXITCODE
        }
        finally {
            $env:DOTNET_ROLL_FORWARD = $previousRollForward
            $env:CL = $previousCompilerOptions
        }

        if ($uatExitCode -ne 0) { throw "RunUAT BuildPlugin failed with exit code $uatExitCode." }
        $packagedPlugin = $packageRoot
        if (-not (Test-Path -LiteralPath (Join-Path $packagedPlugin 'GeneralBlueprint.uplugin'))) {
            throw "UAT reported success but the packaged plugin was not found at $packagedPlugin."
        }

        # BuildPlugin may omit files that are required for a Fab code-plugin
        # submission even when they are present in the staged source plugin.
        # Keep the UAT-built binaries and source intact, then add these exact
        # release support files before creating the single-plugin archive.
        $requiredReleaseFiles = @(
            'Config\DefaultEditor.ini',
            'Resources\Icon128.png',
            'README.md',
            'Docs\Compatibility.md',
            'Docs\UserGuide.md',
            'Docs\UserGuide.txt',
            'Docs\UserGuide.zh-CN.md'
            'Docs\UEMCP.zh-CN.md'
        )
        foreach ($relativePath in $requiredReleaseFiles) {
            $stagedFile = Join-Path $stageRoot $relativePath
            $packagedFile = Join-Path $packagedPlugin $relativePath
            if (-not (Test-Path -LiteralPath $stagedFile -PathType Leaf)) {
                throw "Required release file is missing from staging: $relativePath"
            }
            if (-not (Test-Path -LiteralPath $packagedFile -PathType Leaf)) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $packagedFile) -Force | Out-Null
                Copy-Item -LiteralPath $stagedFile -Destination $packagedFile
            }
        }
        foreach ($requiredDirectory in @('Source', 'Content', 'Config', 'Resources')) {
            if (-not (Test-Path -LiteralPath (Join-Path $packagedPlugin $requiredDirectory) -PathType Container)) {
                throw "Required Fab code-plugin directory is missing: $requiredDirectory"
            }
        }

        Compress-Archive -Path $packagedPlugin -DestinationPath $zipPath -CompressionLevel Optimal
        $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText("$zipPath.sha256", "$hash  $(Split-Path -Leaf $zipPath)`n", [Text.UTF8Encoding]::new($false))
        $results.Add([pscustomobject]@{
            Engine = $version
            EnginePatch = $engineVersion.PatchVersion
            Status = 'packaged'
            Package = $zipPath
            Sha256 = $hash
            Log = $versionLog
            ElapsedSeconds = [math]::Round(((Get-Date) - $runStarted).TotalSeconds, 1)
            Error = ''
        })
        Write-Host "UE $version package created: $zipPath"

        if (-not $KeepWork) {
            $resolvedWork = [IO.Path]::GetFullPath($workRoot)
            $workBase = [IO.Path]::GetFullPath((Join-Path $PluginRoot '.buildwork'))
            if (-not $resolvedWork.StartsWith(($workBase + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clean work path outside .buildwork: $resolvedWork"
            }
            Remove-Item -LiteralPath $resolvedWork -Recurse -Force
        }
    }
    catch {
        $results.Add([pscustomobject]@{
            Engine = $version
            Status = 'failed'
            Package = ''
            Sha256 = ''
            Log = $versionLog
            ElapsedSeconds = [math]::Round(((Get-Date) - $runStarted).TotalSeconds, 1)
            Error = $_.Exception.Message
        })
        Write-Warning "UE $version packaging failed: $($_.Exception.Message)"
    }
    Save-Manifest -Items $results -Path $manifestPath
}

$failed = @($results | Where-Object Status -ne 'packaged')
Write-Host "Packaged $(@($results | Where-Object Status -eq 'packaged').Count) of $($results.Count) requested engine versions. Manifest: $manifestPath"
if ($failed.Count -gt 0) {
    throw "$($failed.Count) engine package(s) failed. Review the manifest and per-version logs."
}
