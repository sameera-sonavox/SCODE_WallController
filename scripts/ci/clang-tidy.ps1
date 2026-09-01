[CmdletBinding()]
param(
    [string] $EnvironmentFile,
    [string] $BuildDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Stage {
    param([Parameter(Mandatory)][string] $Message)

    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Get-RequiredProperty {
    param(
        [Parameter(Mandatory)][object] $Object,
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][string] $Context
    )

    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value) {
        throw "Required property '$Context.$Name' is missing."
    }
    if ($property.Value -is [string] -and [string]::IsNullOrWhiteSpace($property.Value)) {
        throw "Required property '$Context.$Name' is empty."
    }

    return $property.Value
}

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string] $Path)

    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-SinglePath {
    param(
        [Parameter(Mandatory)][AllowNull()][object] $Value,
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][ValidateSet('Leaf', 'Container')][string] $PathType
    )

    $values = @($Value)
    if ($values.Count -ne 1 -or $null -eq $values[0]) {
        throw "$Name must contain exactly one non-null path."
    }

    $path = Get-NormalizedPath -Path ([string]$values[0])
    if (-not (Test-Path -LiteralPath $path -PathType $PathType)) {
        throw "$Name path does not exist as a $PathType`: $path"
    }
    return $path
}

function Test-PathAtOrBelow {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $Root
    )

    $pathFull = Get-NormalizedPath -Path $Path
    $rootFull = Get-NormalizedPath -Path $Root
    return (
        $pathFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        $pathFull.StartsWith("$rootFull\", [StringComparison]::OrdinalIgnoreCase)
    )
}

function Get-ClangTidyVersion {
    param(
        [Parameter(Mandatory)][string] $ClangTidy,
        [Parameter(Mandatory)][string] $ExpectedVersion
    )

    $output = @(& $ClangTidy '--version' 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "clang-tidy --version failed with exit code $exitCode."
    }

    $text = ($output -join "`n").Trim()
    if ($text -notmatch "(?<![0-9])$([regex]::Escape($ExpectedVersion))(?![0-9])") {
        throw "clang-tidy version mismatch. Expected $ExpectedVersion, output was: $text"
    }
    return $ExpectedVersion
}

function Resolve-CompilationFile {
    param([Parameter(Mandatory)][object] $Entry)

    $file = [string](Get-RequiredProperty -Object $Entry -Name 'file' -Context 'compile_commands entry')
    if ([IO.Path]::IsPathRooted($file)) {
        return Get-NormalizedPath -Path $file
    }

    $directory = [string](Get-RequiredProperty -Object $Entry -Name 'directory' -Context 'compile_commands entry')
    return Get-NormalizedPath -Path (Join-Path $directory $file)
}

function ConvertTo-ClangCompatibleEntry {
    param(
        [Parameter(Mandatory)][object] $Entry,
        [Parameter(Mandatory)][AllowEmptyCollection()][Collections.Generic.HashSet[string]] $RemovedFlags
    )

    # Preserve the generated compilation command and remove only GCC driver flags
    # that clang cannot parse. Includes, definitions, target flags, language mode,
    # sysroot, and every other firmware compile option remain unchanged.
    $gccOnlyPatterns = @(
        '^-fno-printf-return-value$',
        '^--param=.+$',
        '^-fno-reorder-functions$',
        '^-fno-defer-pop$',
        '^-specs=.+$'
    )
    $converted = [ordered]@{}
    foreach ($property in $Entry.PSObject.Properties) {
        $converted[$property.Name] = $property.Value
    }

    $argumentsProperty = $Entry.PSObject.Properties['arguments']
    $commandProperty = $Entry.PSObject.Properties['command']
    if ($null -ne $argumentsProperty -and $null -ne $argumentsProperty.Value) {
        $arguments = [Collections.Generic.List[string]]::new()
        foreach ($argumentValue in @($argumentsProperty.Value)) {
            $argument = [string]$argumentValue
            if ($gccOnlyPatterns | Where-Object { $argument -match $_ }) {
                [void]$RemovedFlags.Add($argument)
            }
            else {
                [void]$arguments.Add($argument)
            }
        }
        $converted['arguments'] = @($arguments)
    }
    elseif ($null -ne $commandProperty -and -not [string]::IsNullOrWhiteSpace([string]$commandProperty.Value)) {
        $command = [string]$commandProperty.Value
        $commandPatterns = @(
            '(?<!\S)-fno-printf-return-value(?=\s|$)',
            '(?<!\S)--param=\S+(?=\s|$)',
            '(?<!\S)-fno-reorder-functions(?=\s|$)',
            '(?<!\S)-fno-defer-pop(?=\s|$)',
            '(?<!\S)-specs=(?:"[^"]*"|\S+)(?=\s|$)'
        )
        foreach ($pattern in $commandPatterns) {
            foreach ($match in [regex]::Matches($command, $pattern)) {
                [void]$RemovedFlags.Add($match.Value)
            }
            $command = [regex]::Replace($command, $pattern, '')
        }
        $converted['command'] = $command
    }
    else {
        throw 'Compilation database entry contains neither command nor arguments.'
    }

    return [pscustomobject]$converted
}

function Invoke-ClangTidyAnalysis {
    $repositoryRoot = Get-NormalizedPath -Path (Join-Path $PSScriptRoot '..\..')
    if ([string]::IsNullOrWhiteSpace($EnvironmentFile)) {
        $EnvironmentFile = Join-Path (Split-Path -Parent $repositoryRoot) '_ci\bootstrap-environment.json'
    }

    $environmentPath = Assert-SinglePath -Value $EnvironmentFile -Name 'Bootstrap environment file' -PathType Leaf
    $configPath = Assert-SinglePath -Value (Join-Path $repositoryRoot '.clang-tidy') -Name 'clang-tidy configuration' -PathType Leaf
    $toolchainPath = Assert-SinglePath -Value (Join-Path $repositoryRoot 'ci\toolchain.json') -Name 'Toolchain manifest' -PathType Leaf

    Write-Stage 'Loading and validating the clang-tidy environment'
    $environment = Get-Content -Raw -LiteralPath $environmentPath | ConvertFrom-Json
    $toolchain = Get-Content -Raw -LiteralPath $toolchainPath | ConvertFrom-Json
    if ($toolchain.schema_version -ne 1) {
        throw 'Unsupported toolchain.json schema_version.'
    }

    foreach ($name in @('repository_root', 'build_root', 'clang_tidy', 'verified_versions')) {
        [void](Get-RequiredProperty -Object $environment -Name $name -Context 'bootstrap environment')
    }
    $environmentRepository = Assert-SinglePath -Value $environment.repository_root -Name 'Bootstrap repository root' -PathType Container
    if (-not $environmentRepository.Equals($repositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Bootstrap environment repository mismatch. Expected '$repositoryRoot', got '$environmentRepository'."
    }

    $buildRoot = Assert-SinglePath -Value $environment.build_root -Name 'Build root' -PathType Container
    $clangTidy = Assert-SinglePath -Value $environment.clang_tidy -Name 'clang-tidy' -PathType Leaf
    $expectedVersion = [string](Get-RequiredProperty -Object $toolchain.llvm -Name 'version' -Context 'toolchain.llvm')
    $environmentVersion = [string](Get-RequiredProperty -Object $environment.verified_versions -Name 'clang_tidy' -Context 'bootstrap environment.verified_versions')
    if (-not $environmentVersion.Equals($expectedVersion, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Bootstrap clang-tidy version is stale. Expected '$expectedVersion', got '$environmentVersion'. Rerun bootstrap.ps1."
    }
    $clangTidyVersion = Get-ClangTidyVersion -ClangTidy $clangTidy -ExpectedVersion $expectedVersion
    Write-Host "clang-tidy version verified: $clangTidyVersion"

    if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
        $BuildDirectory = Join-Path $buildRoot 'wallcontroller'
    }
    $buildDirectoryFull = Assert-SinglePath -Value $BuildDirectory -Name 'Firmware build directory' -PathType Container
    if (-not (Test-PathAtOrBelow -Path $buildDirectoryFull -Root $buildRoot)) {
        throw "Firmware build directory is outside the prepared build root: $buildDirectoryFull"
    }

    $compileDatabasePath = Assert-SinglePath -Value (Join-Path $buildDirectoryFull 'compile_commands.json') -Name 'Compilation database' -PathType Leaf
    $buildResultPath = Assert-SinglePath -Value (Join-Path $buildDirectoryFull 'build-result.json') -Name 'Firmware build result' -PathType Leaf
    $buildResult = Get-Content -Raw -LiteralPath $buildResultPath | ConvertFrom-Json
    $applicationSource = Assert-SinglePath -Value (Get-RequiredProperty -Object $buildResult -Name 'application_source' -Context 'build-result') -Name 'Application source' -PathType Container
    $applicationSrc = Assert-SinglePath -Value (Join-Path $applicationSource 'src') -Name 'Application src directory' -PathType Container

    Write-Stage 'Selecting WallController C translation units'
    $compileCommands = @(Get-Content -Raw -LiteralPath $compileDatabasePath | ConvertFrom-Json)
    if ($compileCommands.Count -eq 0) {
        throw 'Compilation database is empty.'
    }

    $selectedEntries = [Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $compileCommands) {
        $file = Resolve-CompilationFile -Entry $entry
        if ([IO.Path]::GetExtension($file).Equals('.c', [StringComparison]::OrdinalIgnoreCase) -and
            (Test-PathAtOrBelow -Path $file -Root $applicationSrc)) {
            if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
                throw "Selected application translation unit is missing: $file"
            }
            $selectedEntries[$file] = $entry
        }
    }

    $translationUnits = @($selectedEntries.Keys | Sort-Object)
    if ($translationUnits.Count -eq 0) {
        throw "No application C translation units under '$applicationSrc' were found in compile_commands.json."
    }
    Write-Host "Selected application translation units: $($translationUnits.Count)"

    $analysisRoot = Get-NormalizedPath -Path (Join-Path $buildDirectoryFull 'clang-tidy-analysis')
    if (-not (Test-PathAtOrBelow -Path $analysisRoot -Root $buildDirectoryFull)) {
        throw "Unsafe clang-tidy analysis directory: $analysisRoot"
    }
    if (Test-Path -LiteralPath $analysisRoot) {
        Remove-Item -LiteralPath $analysisRoot -Recurse -Force
    }
    $fixesRoot = Join-Path $analysisRoot 'fixes'
    New-Item -ItemType Directory -Path $fixesRoot -Force | Out-Null

    $clangDatabaseRoot = Join-Path $analysisRoot 'compile-database'
    New-Item -ItemType Directory -Path $clangDatabaseRoot -Force | Out-Null
    $removedFlags = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    $clangDatabaseEntries = @(
        foreach ($file in $translationUnits) {
            ConvertTo-ClangCompatibleEntry -Entry $selectedEntries[$file] -RemovedFlags $removedFlags
        }
    )
    [IO.File]::WriteAllText(
        (Join-Path $clangDatabaseRoot 'compile_commands.json'),
        ($clangDatabaseEntries | ConvertTo-Json -Depth 8),
        [Text.UTF8Encoding]::new($false)
    )
    if ($removedFlags.Count -gt 0) {
        Write-Host "Removed GCC-only driver flags for clang parsing: $(@($removedFlags | Sort-Object) -join ', ')"
    }

    $logPath = Join-Path $buildDirectoryFull 'clang-tidy.log'
    $resultPath = Join-Path $buildDirectoryFull 'clang-tidy-result.json'
    foreach ($generatedFile in @($logPath, $resultPath)) {
        if (Test-Path -LiteralPath $generatedFile) {
            Remove-Item -LiteralPath $generatedFile -Force
        }
    }

    $headerSegments = @(Get-NormalizedPath -Path $applicationSrc) -split '[\\/]'
    $headerFilter = '^' + (($headerSegments | ForEach-Object { [regex]::Escape($_) }) -join '[\\/]') + '[\\/].*'
    $diagnostics = [Collections.Generic.List[string]]::new()
    $findingCount = 0
    $relativeTranslationUnits = [Collections.Generic.List[string]]::new()

    Write-Stage 'Running clang-tidy in reporting-only mode'
    for ($index = 0; $index -lt $translationUnits.Count; $index++) {
        $file = $translationUnits[$index]
        $relative = [IO.Path]::GetRelativePath($applicationSource, $file).Replace('\', '/')
        [void]$relativeTranslationUnits.Add($relative)
        $fixesPath = Join-Path $fixesRoot ('{0:D4}.yaml' -f ($index + 1))
        $arguments = @(
            "--config-file=$configPath",
            "--header-filter=$headerFilter",
            "--export-fixes=$fixesPath",
            '--use-color=false',
            '--quiet',
            '--extra-arg-before=--target=arm-none-eabi',
            "-p=$clangDatabaseRoot",
            $file
        )

        Write-Host "[$($index + 1)/$($translationUnits.Count)] clang-tidy $relative"
        $output = @(& $clangTidy @arguments 2>&1)
        $exitCode = $LASTEXITCODE
        $plainOutput = [Collections.Generic.List[string]]::new()
        foreach ($line in $output) {
            $plainLine = ([string]$line) -replace "`e\[[0-9;]*[A-Za-z]", ''
            [void]$plainOutput.Add($plainLine)
            Write-Host $plainLine
            Add-Content -LiteralPath $logPath -Value $plainLine -Encoding utf8
            if ($plainLine -match '(?i):\d+:\d+:\s+warning:\s+') {
                $displayLine = $plainLine.Replace($applicationSource, 'hello_world', [StringComparison]::OrdinalIgnoreCase)
                [void]$diagnostics.Add($displayLine)
            }
        }

        if ($exitCode -ne 0) {
            throw "clang-tidy infrastructure failure for '$relative' (exit code $exitCode)."
        }
        $fatalOutput = @($plainOutput | Where-Object {
            $_ -match '(?i)(?:^|:\d+:\d+:\s+)(?:fatal\s+)?error:\s+' -or
            $_ -match '(?i)^Error while processing\s+' -or
            $_ -match '(?i)^LLVM ERROR:'
        })
        if ($fatalOutput.Count -gt 0) {
            throw "clang-tidy reported a parse or infrastructure error for '$relative'."
        }

        if (Test-Path -LiteralPath $fixesPath -PathType Leaf) {
            $fixesText = Get-Content -Raw -LiteralPath $fixesPath
            $findingCount += [regex]::Matches($fixesText, '(?m)^\s*-\s+DiagnosticName:\s+').Count
        }
    }

    $result = [ordered]@{
        status               = 'PASS'
        enforcement          = 'reporting_only'
        clang_tidy_version   = $clangTidyVersion
        files_analyzed       = $translationUnits.Count
        finding_count        = $findingCount
        removed_gcc_flags    = @($removedFlags | Sort-Object)
        translation_units    = @($relativeTranslationUnits)
        diagnostics          = @($diagnostics)
    }
    [IO.File]::WriteAllText(
        $resultPath,
        ($result | ConvertTo-Json -Depth 6),
        [Text.UTF8Encoding]::new($false)
    )

    Write-Stage 'clang-tidy analysis completed successfully'
    Write-Host "Files analyzed : $($translationUnits.Count)"
    Write-Host "Findings       : $findingCount"
    Write-Host 'Enforcement    : reporting only'
    Write-Host "Result         : $resultPath"
    Write-Host "Log            : $logPath"
}

try {
    Invoke-ClangTidyAnalysis
}
catch {
    Write-Error $_
    exit 1
}
