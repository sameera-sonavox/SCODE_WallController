[CmdletBinding()]
param(
    [string] $Root,
    [switch] $Clean,
    [string] $GitHubToken = $env:GITHUB_TOKEN
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:VerifiedDownloads = [System.Collections.Generic.List[string]]::new()

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

function Assert-Sha256Value {
    param(
        [Parameter(Mandatory)][string] $Value,
        [Parameter(Mandatory)][string] $Context
    )

    if ($Value -notmatch '^[0-9A-Fa-f]{64}$') {
        throw "$Context is not a 64-character SHA-256 value."
    }
}

function Get-FileSha256 {
    param([Parameter(Mandatory)][string] $Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Assert-FileHash {
    param(
        [Parameter(Mandatory)][string] $Path,
        [Parameter(Mandatory)][string] $ExpectedSha256
    )

    Assert-Sha256Value -Value $ExpectedSha256 -Context "Expected hash for $Path"
    $actual = Get-FileSha256 -Path $Path
    if (-not $actual.Equals($ExpectedSha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw "SHA-256 mismatch for '$Path'. Expected $ExpectedSha256, got $actual."
    }

    Write-Host "SHA-256 verified: $Path"
    return $actual
}

function Get-VerifiedDownload {
    param(
        [Parameter(Mandatory)][string] $Url,
        [Parameter(Mandatory)][string] $DestinationPath,
        [Parameter(Mandatory)][string] $ExpectedSha256
    )

    $destinationDirectory = Split-Path -Parent $DestinationPath
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null

    if (Test-Path -LiteralPath $DestinationPath) {
        try {
            [void](Assert-FileHash -Path $DestinationPath -ExpectedSha256 $ExpectedSha256)
            Write-Host "Using cached verified download: $DestinationPath"
            [void]$script:VerifiedDownloads.Add($DestinationPath)
            return $DestinationPath
        }
        catch {
            throw "Existing cached download is invalid. Use -Clean or remove only this file and retry. $($_.Exception.Message)"
        }
    }

    $partialPath = "$DestinationPath.partial"
    if (Test-Path -LiteralPath $partialPath) {
        Remove-Item -LiteralPath $partialPath -Force
    }

    Write-Host "Downloading: $Url"
    try {
        Invoke-WebRequest -Uri $Url -OutFile $partialPath -UseBasicParsing
        [void](Assert-FileHash -Path $partialPath -ExpectedSha256 $ExpectedSha256)
        Move-Item -LiteralPath $partialPath -Destination $DestinationPath
    }
    finally {
        if (Test-Path -LiteralPath $partialPath) {
            Remove-Item -LiteralPath $partialPath -Force
        }
    }

    Write-Host "Downloaded and verified: $DestinationPath"
    [void]$script:VerifiedDownloads.Add($DestinationPath)
    return $DestinationPath
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter()][string[]] $Arguments = @()
    )

    Write-Host "> $FilePath $($Arguments -join ' ')"
    $output = @(& $FilePath @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    foreach ($line in $output) {
        Write-Host $line
    }
    if ($exitCode -ne 0) {
        throw "Command failed with exit code $exitCode`: $FilePath $($Arguments -join ' ')"
    }
}

function Get-CheckedOutput {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter()][string[]] $Arguments = @()
    )

    $output = @(& $FilePath @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($Arguments -join ' ')`n$($output -join "`n")"
    }

    return ($output -join "`n").Trim()
}

function Assert-VersionOutput {
    param(
        [Parameter(Mandatory)][string] $Output,
        [Parameter(Mandatory)][string] $ExpectedVersion,
        [Parameter(Mandatory)][string] $Component
    )

    if ($Output -notmatch "(?<![0-9])$([regex]::Escape($ExpectedVersion))(?![0-9])") {
        throw "$Component version mismatch. Expected $ExpectedVersion, output was: $Output"
    }

    Write-Host "$Component version verified: $ExpectedVersion"
}

function Assert-SinglePath {
    param(
        [Parameter(Mandatory)][AllowNull()][object] $Value,
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][ValidateSet('Leaf', 'Container')][string] $PathType
    )

    $values = @($Value)
    if ($values.Count -ne 1) {
        throw "$Name must contain exactly one path value; found $($values.Count). This may indicate success-stream contamination."
    }
    if ($null -eq $values[0]) {
        throw "$Name path is null."
    }

    $path = [string]$values[0]
    if ([string]::IsNullOrWhiteSpace($path)) {
        throw "$Name path is empty or whitespace."
    }

    $fullPath = [IO.Path]::GetFullPath($path)
    if (-not (Test-Path -LiteralPath $fullPath -PathType $PathType)) {
        throw "$Name path does not exist as a $PathType`: $fullPath"
    }

    Write-Host "$Name path verified: $fullPath"
    return $fullPath
}

function Get-SevenZipPath {
    $command = Get-Command 7z.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $command) {
        return $command.Source
    }

    $candidates = [System.Collections.Generic.List[string]]::new()
    $programFiles = [Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFiles)
    if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
        [void]$candidates.Add((Join-Path $programFiles '7-Zip\7z.exe'))
    }

    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        [void]$candidates.Add((Join-Path $programFilesX86 '7-Zip\7z.exe'))
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw '7z.exe is required for .7z and .tar.zst archives but was not found on PATH or in a standard Program Files location.'
}

function New-StagingDirectory {
    param(
        [Parameter(Mandatory)][string] $StagingRoot,
        [Parameter(Mandatory)][string] $Name
    )

    New-Item -ItemType Directory -Path $StagingRoot -Force | Out-Null
    $path = Join-Path $StagingRoot "$Name-$([Guid]::NewGuid().ToString('N'))"
    New-Item -ItemType Directory -Path $path | Out-Null
    return $path
}

function Assert-InstallDestinationAvailable {
    param([Parameter(Mandatory)][string] $Destination)

    if (-not (Test-Path -LiteralPath $Destination)) {
        return
    }

    if (@(Get-ChildItem -LiteralPath $Destination -Force).Count -ne 0) {
        throw "Installation destination contains unrecognized or partial data: $Destination. Use -Clean for a generated-root rebuild."
    }

    Remove-Item -LiteralPath $Destination
}

function Expand-ZipTool {
    param(
        [Parameter(Mandatory)][string] $Archive,
        [Parameter(Mandatory)][string] $Destination,
        [Parameter(Mandatory)][string] $StagingRoot,
        [Parameter(Mandatory)][bool] $UseSingleArchiveRoot
    )

    Assert-InstallDestinationAvailable -Destination $Destination
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    $stage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'zip'
    try {
        Expand-Archive -LiteralPath $Archive -DestinationPath $stage
        if ($UseSingleArchiveRoot) {
            $children = @(Get-ChildItem -LiteralPath $stage -Force)
            if ($children.Count -ne 1 -or -not $children[0].PSIsContainer) {
                throw "Expected one top-level directory in archive '$Archive'."
            }
            Move-Item -LiteralPath $children[0].FullName -Destination $Destination
        }
        else {
            Move-Item -LiteralPath $stage -Destination $Destination
            $stage = $null
        }
    }
    finally {
        if ($null -ne $stage -and (Test-Path -LiteralPath $stage)) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
    }
}

function Expand-TarZstInto {
    param(
        [Parameter(Mandatory)][string] $SevenZip,
        [Parameter(Mandatory)][string] $Archive,
        [Parameter(Mandatory)][string] $Destination,
        [Parameter(Mandatory)][string] $StagingRoot
    )

    $stage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'tar-zst'
    try {
        Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', "-o$stage", $Archive)
        $tarFiles = @(Get-ChildItem -LiteralPath $stage -File -Filter '*.tar')
        if ($tarFiles.Count -ne 1) {
            throw "Expected one .tar payload after decompressing '$Archive', found $($tarFiles.Count)."
        }
        New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', '-aoa', "-o$Destination", $tarFiles[0].FullName)
    }
    finally {
        if (Test-Path -LiteralPath $stage) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
    }
}

function Install-PythonFromNuGetPackage {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $Package,
        [Parameter(Mandatory)][string] $PythonRoot,
        [Parameter(Mandatory)][string] $StagingRoot
    )

    $pythonExe = Join-Path $PythonRoot 'tools\python.exe'
    if (Test-Path -LiteralPath $pythonExe) {
        $output = Get-CheckedOutput -FilePath $pythonExe -Arguments @('--version')
        if ($output -ne "Python $($Manifest.version)") {
            throw "Python version mismatch. Expected exactly 'Python $($Manifest.version)', output was: $output"
        }
        Write-Host "Python version verified: $($Manifest.version)"
        return $pythonExe
    }

    Assert-InstallDestinationAvailable -Destination $PythonRoot
    $stage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'python-nuget'
    try {
        Write-Host "Extracting verified Python NuGet package: $Package"
        [IO.Compression.ZipFile]::ExtractToDirectory($Package, $stage)

        $candidates = @(
            Get-ChildItem -LiteralPath $stage -Recurse -File -Filter 'python.exe' |
                Where-Object {
                    [IO.Path]::GetRelativePath($stage, $_.FullName).Replace('/', '\') -ieq 'tools\python.exe'
                }
        )
        if ($candidates.Count -ne 1) {
            throw "Expected exactly one tools\python.exe in Python NuGet package '$Package', found $($candidates.Count)."
        }

        New-Item -ItemType Directory -Path (Split-Path -Parent $PythonRoot) -Force | Out-Null
        Move-Item -LiteralPath $stage -Destination $PythonRoot
        $stage = $null
    }
    finally {
        if ($null -ne $stage -and (Test-Path -LiteralPath $stage)) {
            Remove-Item -LiteralPath $stage -Recurse -Force
        }
    }

    if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
        throw "Python NuGet extraction is incomplete; tools\python.exe is missing at $pythonExe"
    }

    $output = Get-CheckedOutput -FilePath $pythonExe -Arguments @('--version')
    if ($output -ne "Python $($Manifest.version)") {
        throw "Python version mismatch. Expected exactly 'Python $($Manifest.version)', output was: $output"
    }
    Write-Host "Python version verified: $($Manifest.version)"
    return $pythonExe
}

function Initialize-PythonEnvironment {
    param(
        [Parameter(Mandatory)][object] $Toolchain,
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $PythonExe,
        [Parameter(Mandatory)][string] $VenvRoot
    )

    $venvPython = Join-Path $VenvRoot 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $venvPython)) {
        Assert-InstallDestinationAvailable -Destination $VenvRoot
        Invoke-Checked -FilePath $PythonExe -Arguments @('-m', 'venv', $VenvRoot)
    }

    $venvConfig = Join-Path $VenvRoot 'pyvenv.cfg'
    if (-not (Test-Path -LiteralPath $venvConfig)) {
        throw "Virtual environment configuration is missing: $venvConfig"
    }

    $venvConfigText = Get-Content -Raw -LiteralPath $venvConfig
    if ($venvConfigText -notlike "*$([IO.Path]::GetDirectoryName($PythonExe))*") {
        throw 'Existing virtual environment was not created from the bootstrapped Python installation.'
    }

    $lockRelative = $Toolchain.python.requirements_lock.path.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $lockPath = Join-Path $RepositoryRoot $lockRelative
    Invoke-Checked -FilePath $venvPython -Arguments @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-input', "pip==$($Toolchain.python.pip_version)")
    Invoke-Checked -FilePath $venvPython -Arguments @('-m', 'pip', 'install', '--disable-pip-version-check', '--no-input', '-r', $lockPath)
    Invoke-Checked -FilePath $venvPython -Arguments @('-m', 'pip', 'check')

    $pythonOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('--version')
    Assert-VersionOutput -Output $pythonOutput -ExpectedVersion $Toolchain.python.version -Component 'Venv Python'
    $pipOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('-m', 'pip', '--version')
    Assert-VersionOutput -Output $pipOutput -ExpectedVersion $Toolchain.python.pip_version -Component 'pip'
    $westOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('-m', 'west', '--version')
    Assert-VersionOutput -Output $westOutput -ExpectedVersion $Toolchain.zephyr.west_version -Component 'west'
    return $venvPython
}

function Install-CMake {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $Archive,
        [Parameter(Mandatory)][string] $ToolsRoot,
        [Parameter(Mandatory)][string] $StagingRoot
    )

    $root = Join-Path $ToolsRoot "cmake\$($Manifest.version)"
    $exe = Join-Path $root 'bin\cmake.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        Expand-ZipTool -Archive $Archive -Destination $root -StagingRoot $StagingRoot -UseSingleArchiveRoot $true
    }
    $output = Get-CheckedOutput -FilePath $exe -Arguments @('--version')
    Assert-VersionOutput -Output $output -ExpectedVersion $Manifest.version -Component 'CMake'
    return $exe
}

function Install-Ninja {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $Archive,
        [Parameter(Mandatory)][string] $ToolsRoot,
        [Parameter(Mandatory)][string] $StagingRoot
    )

    $root = Join-Path $ToolsRoot "ninja\$($Manifest.version)"
    $exe = Join-Path $root 'ninja.exe'
    if (-not (Test-Path -LiteralPath $exe)) {
        Expand-ZipTool -Archive $Archive -Destination $root -StagingRoot $StagingRoot -UseSingleArchiveRoot $false
    }
    $output = Get-CheckedOutput -FilePath $exe -Arguments @('--version')
    Assert-VersionOutput -Output $output -ExpectedVersion $Manifest.version -Component 'Ninja'
    return $exe
}

function Install-ClangTidy {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $Package,
        [Parameter(Mandatory)][string] $ToolsRoot,
        [Parameter(Mandatory)][string] $StagingRoot,
        [Parameter(Mandatory)][string] $SevenZip
    )

    if ($Manifest.artifact.package_type -ne 'nsis-7zip-extractable') {
        throw "Unsupported LLVM package type: $($Manifest.artifact.package_type)"
    }
    if ($Manifest.artifact.architecture -ne 'x86_64') {
        throw "Unsupported LLVM package architecture: $($Manifest.artifact.architecture)"
    }

    $root = Join-Path $ToolsRoot "llvm\$($Manifest.version)"
    $executableRelative = $Manifest.clang_tidy_executable.Replace('/', '\')
    $exe = Join-Path $root $executableRelative
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        Assert-InstallDestinationAvailable -Destination $root
        New-Item -ItemType Directory -Path (Split-Path -Parent $root) -Force | Out-Null
        $stage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'llvm'
        try {
            Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', "-o$stage", $Package)
            $stagedExe = Join-Path $stage $executableRelative
            if (-not (Test-Path -LiteralPath $stagedExe -PathType Leaf)) {
                throw "Official LLVM package does not contain the expected clang-tidy executable: $executableRelative"
            }
            Move-Item -LiteralPath $stage -Destination $root
            $stage = $null
        }
        finally {
            if ($null -ne $stage -and (Test-Path -LiteralPath $stage)) {
                Remove-Item -LiteralPath $stage -Recurse -Force
            }
        }
    }

    $output = Get-CheckedOutput -FilePath $exe -Arguments @('--version')
    Assert-VersionOutput -Output $output -ExpectedVersion $Manifest.version -Component 'clang-tidy'
    return $exe
}

function Install-DtcRuntime {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $PackageArchive,
        [Parameter(Mandatory)][string] $ToolsRoot,
        [Parameter(Mandatory)][string] $StagingRoot,
        [Parameter(Mandatory)][string] $SevenZip
    )

    $root = Join-Path $ToolsRoot "dtc\$($Manifest.version)"
    $payloadRoot = Join-Path $root 'tools'
    $binRoot = Join-Path $payloadRoot 'usr\bin'
    $dtcExe = Join-Path $binRoot 'dtc.exe'
    $requiredFiles = @('dtc.exe', 'msys-2.0.dll', 'msys-yaml-0-2.dll', 'libfdt.dll', 'cygpath.exe')
    $runtimeValid = $true
    foreach ($fileName in $requiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $binRoot $fileName))) {
            $runtimeValid = $false
        }
    }

    $packageStage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'dtc-nupkg'
    try {
        Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', "-o$packageStage", $PackageArchive)
        $verifiedPayloads = [ordered]@{}
        foreach ($payload in $Manifest.runtime_payloads) {
            if ($payload.source -ne 'embedded_in_package') {
                throw "Unsupported DTC payload source for '$($payload.filename)': $($payload.source)"
            }
            $matches = @(Get-ChildItem -LiteralPath $packageStage -Recurse -File | Where-Object { $_.Name -ceq $payload.filename })
            if ($matches.Count -ne 1) {
                throw "Expected exactly one embedded DTC payload named '$($payload.filename)', found $($matches.Count)."
            }
            [void](Assert-FileHash -Path $matches[0].FullName -ExpectedSha256 $payload.sha256)
            Write-Host "Embedded DTC payload verified: $($payload.filename)"
            $verifiedPayloads[$payload.filename] = $matches[0].FullName
        }

        if (-not $runtimeValid) {
            Assert-InstallDestinationAvailable -Destination $root
            New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
            foreach ($payload in $Manifest.runtime_payloads) {
                $archive = [string]$verifiedPayloads[$payload.filename]
                Expand-TarZstInto -SevenZip $SevenZip -Archive $archive -Destination $payloadRoot -StagingRoot $StagingRoot
            }
        }
    }
    finally {
        if (Test-Path -LiteralPath $packageStage) {
            Remove-Item -LiteralPath $packageStage -Recurse -Force
        }
    }

    foreach ($fileName in $requiredFiles) {
        $requiredPath = Join-Path $binRoot $fileName
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            throw "DTC runtime is missing required file: $requiredPath"
        }
    }

    $output = Get-CheckedOutput -FilePath $dtcExe -Arguments @('--version')
    Assert-VersionOutput -Output $output -ExpectedVersion $Manifest.version -Component 'DTC'
    return $dtcExe
}

function Install-ZephyrSdk {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $MinimalArchive,
        [Parameter(Mandatory)][string] $ToolchainArchive,
        [Parameter(Mandatory)][string] $ToolsRoot,
        [Parameter(Mandatory)][string] $StagingRoot,
        [Parameter(Mandatory)][string] $SevenZip
    )

    $sdkRoot = Join-Path $ToolsRoot "zephyr-sdk\$($Manifest.version)"
    $sdkVersionFile = Join-Path $sdkRoot 'sdk_version'
    $target = $Manifest.target_toolchain.target
    $gnuRoot = Join-Path $sdkRoot "gnu\$target"
    $gccExe = Join-Path $gnuRoot "bin\$target-gcc.exe"

    if (-not ((Test-Path -LiteralPath $sdkVersionFile) -and (Test-Path -LiteralPath $gccExe))) {
        Assert-InstallDestinationAvailable -Destination $sdkRoot
        $minimalStage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'sdk-minimal'
        $toolchainStage = New-StagingDirectory -StagingRoot $StagingRoot -Name 'sdk-toolchain'
        try {
            Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', "-o$minimalStage", $MinimalArchive)
            $minimalRoots = @(Get-ChildItem -LiteralPath $minimalStage -Directory)
            if ($minimalRoots.Count -ne 1) {
                throw "Expected one minimal SDK archive root, found $($minimalRoots.Count)."
            }
            New-Item -ItemType Directory -Path (Split-Path -Parent $sdkRoot) -Force | Out-Null
            Move-Item -LiteralPath $minimalRoots[0].FullName -Destination $sdkRoot

            Invoke-Checked -FilePath $SevenZip -Arguments @('x', '-y', "-o$toolchainStage", $ToolchainArchive)
            $toolchainRoots = @(Get-ChildItem -LiteralPath $toolchainStage -Directory)
            if ($toolchainRoots.Count -ne 1 -or $toolchainRoots[0].Name -ne $target) {
                throw "Expected target toolchain archive root '$target'."
            }
            $gnuParent = Join-Path $sdkRoot 'gnu'
            New-Item -ItemType Directory -Path $gnuParent -Force | Out-Null
            Move-Item -LiteralPath $toolchainRoots[0].FullName -Destination (Join-Path $gnuParent $target)
        }
        finally {
            foreach ($stage in @($minimalStage, $toolchainStage)) {
                if (Test-Path -LiteralPath $stage) {
                    Remove-Item -LiteralPath $stage -Recurse -Force
                }
            }
        }
    }

    if (-not (Test-Path -LiteralPath $sdkVersionFile)) {
        throw "Zephyr SDK version file is missing: $sdkVersionFile"
    }
    $actualSdkVersion = (Get-Content -Raw -LiteralPath $sdkVersionFile).Trim()
    if ($actualSdkVersion -ne $Manifest.version) {
        throw "Zephyr SDK version mismatch. Expected $($Manifest.version), got $actualSdkVersion."
    }
    Write-Host "Zephyr SDK version verified: $actualSdkVersion"

    $gccOutput = Get-CheckedOutput -FilePath $gccExe -Arguments @('--version')
    Assert-VersionOutput -Output $gccOutput -ExpectedVersion $Manifest.target_toolchain.gcc_version -Component 'arm-zephyr-eabi GCC'

    $armTools = [ordered]@{}
    foreach ($tool in @('ld', 'ar', 'ranlib', 'objcopy', 'readelf')) {
        $toolPath = Join-Path $gnuRoot "bin\$target-$tool.exe"
        if (-not (Test-Path -LiteralPath $toolPath)) {
            throw "Zephyr SDK ARM tool is missing: $toolPath"
        }
        $armTools[$tool] = $toolPath
    }
    $ldOutput = Get-CheckedOutput -FilePath $armTools['ld'] -Arguments @('--version')
    Assert-VersionOutput -Output $ldOutput -ExpectedVersion $Manifest.target_toolchain.binutils_version -Component 'arm-zephyr-eabi binutils'

    return [pscustomobject]@{
        Root = $sdkRoot
        Gcc  = $gccExe
    }
}

function Get-GitHead {
    param(
        [Parameter(Mandatory)][string] $Git,
        [Parameter(Mandatory)][string] $Repository
    )

    return (Get-CheckedOutput -FilePath $Git -Arguments @('-C', $Repository, 'rev-parse', 'HEAD')).ToLowerInvariant()
}

function Initialize-ZephyrWorkspace {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $VenvPython,
        [Parameter(Mandatory)][string] $Git,
        [Parameter(Mandatory)][string] $Workspace
    )

    $zephyrBase = Join-Path $Workspace 'zephyr'
    $westConfig = Join-Path $Workspace '.west\config'
    if (Test-Path -LiteralPath $Workspace) {
        $children = @(Get-ChildItem -LiteralPath $Workspace -Force)
        if ($children.Count -ne 0 -and -not (Test-Path -LiteralPath $westConfig)) {
            throw "Existing Zephyr workspace is not a valid west workspace: $Workspace. Use -Clean only if it is disposable."
        }
    }

    if (-not (Test-Path -LiteralPath $westConfig)) {
        Invoke-Checked -FilePath $VenvPython -Arguments @('-m', 'west', 'init', '-m', $Manifest.repository, '--mr', $Manifest.release, $Workspace)
    }

    if (-not (Test-Path -LiteralPath (Join-Path $zephyrBase '.git'))) {
        throw "Zephyr manifest repository is missing: $zephyrBase"
    }
    $expectedCommit = $Manifest.commit.ToLowerInvariant()
    $head = Get-GitHead -Git $Git -Repository $zephyrBase
    if ($head -ne $expectedCommit) {
        throw "Zephyr HEAD mismatch before west update. Expected $expectedCommit, got $head."
    }
    Write-Host "Zephyr manifest commit verified: $head"

    Push-Location $Workspace
    try {
        Invoke-Checked -FilePath $VenvPython -Arguments @('-m', 'west', 'update')
    }
    finally {
        Pop-Location
    }

    $head = Get-GitHead -Git $Git -Repository $zephyrBase
    if ($head -ne $expectedCommit) {
        throw "Zephyr HEAD mismatch after west update. Expected $expectedCommit, got $head."
    }

    $requiredPaths = @(
        'modules\hal\nxp',
        'modules\hal\nxp\mcux\mcux-sdk-ng',
        'zephyr\boards\nxp\frdm_mcxa156',
        'bootloader\mcuboot',
        'modules\lib\gui\lvgl',
        'modules\fs\littlefs'
    )
    foreach ($relativePath in $requiredPaths) {
        $requiredPath = Join-Path $Workspace $relativePath
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            throw "Required west project content is missing: $requiredPath"
        }
    }

    return $zephyrBase
}

function Invoke-WithGitHubToken {
    param(
        [Parameter(Mandatory)][AllowNull()][AllowEmptyString()][string] $Token,
        [Parameter(Mandatory)][scriptblock] $Operation
    )

    if ([string]::IsNullOrWhiteSpace($Token)) {
        return & $Operation
    }

    $names = @(
        'GIT_ASKPASS',
        'GIT_TERMINAL_PROMPT',
        'GIT_CONFIG_COUNT',
        'GIT_CONFIG_KEY_0',
        'GIT_CONFIG_VALUE_0',
        'MCXA_LIB_GIT_ASKPASS_TOKEN'
    )
    $oldValues = @{}
    foreach ($name in $names) {
        $oldValues[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }

    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    $askPassDirectory = Join-Path $temporaryRoot ("wallcontroller-git-askpass-{0}" -f [guid]::NewGuid().ToString('N'))
    $askPassPath = Join-Path $askPassDirectory 'git-askpass.cmd'
    $askPassContents = @'
@echo off
setlocal
set "git_prompt=%~1"
if /I "%git_prompt:~0,8%"=="Username" (
    echo x-access-token
    exit /b 0
)
if /I "%git_prompt:~0,8%"=="Password" (
    echo(%MCXA_LIB_GIT_ASKPASS_TOKEN%
    exit /b 0
)
exit /b 1
'@

    try {
        New-Item -ItemType Directory -Path $askPassDirectory | Out-Null
        Set-Content -LiteralPath $askPassPath -Value $askPassContents -Encoding ascii -NoNewline

        [Environment]::SetEnvironmentVariable('GIT_ASKPASS', $askPassPath, 'Process')
        [Environment]::SetEnvironmentVariable('GIT_TERMINAL_PROMPT', '0', 'Process')
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_COUNT', '1', 'Process')
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_KEY_0', 'credential.helper', 'Process')
        [Environment]::SetEnvironmentVariable('GIT_CONFIG_VALUE_0', '', 'Process')
        [Environment]::SetEnvironmentVariable('MCXA_LIB_GIT_ASKPASS_TOKEN', $Token, 'Process')
        return & $Operation
    }
    finally {
        foreach ($name in $names) {
            [Environment]::SetEnvironmentVariable($name, $oldValues[$name], 'Process')
        }
        if (Test-Path -LiteralPath $askPassDirectory) {
            $resolvedAskPassDirectory = [IO.Path]::GetFullPath($askPassDirectory)
            if (-not $resolvedAskPassDirectory.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to remove unsafe askpass directory: $resolvedAskPassDirectory"
            }
            Remove-Item -LiteralPath $resolvedAskPassDirectory -Recurse -Force
        }
    }
}

function Initialize-McxaLib {
    param(
        [Parameter(Mandatory)][object] $Manifest,
        [Parameter(Mandatory)][string] $Git,
        [Parameter(Mandatory)][string] $DependencyRoot,
        [Parameter()][string] $Token
    )

    New-Item -ItemType Directory -Path $DependencyRoot -Force | Out-Null
    $repositoryRoot = Join-Path $DependencyRoot 'NXP_MCXA_LIB'
    $repositorySlug = $Manifest.repository.Trim().TrimEnd('/')
    if ($repositorySlug -notmatch '^[^/]+/[^/]+$') {
        throw "Unsupported MCXA_Lib repository identifier: $repositorySlug"
    }
    $repositoryUrl = if ([string]::IsNullOrWhiteSpace($Token)) {
        "git@github.com:$repositorySlug.git"
    }
    else {
        "https://github.com/$repositorySlug.git"
    }

    $operation = {
        if (-not (Test-Path -LiteralPath $repositoryRoot)) {
            Invoke-Checked -FilePath $Git -Arguments @('clone', $repositoryUrl, $repositoryRoot)
        }
        elseif (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot '.git'))) {
            throw "Existing MCXA_Lib destination is not a Git repository: $repositoryRoot"
        }

        $resolvedRepositoryRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path

        $status = Get-CheckedOutput -FilePath $Git -Arguments @('-C', $resolvedRepositoryRoot, 'status', '--porcelain')
        if (-not [string]::IsNullOrWhiteSpace($status)) {
            throw "Existing MCXA_Lib checkout has local changes and will not be overwritten: $resolvedRepositoryRoot"
        }

        $remote = Get-CheckedOutput -FilePath $Git -Arguments @('-C', $resolvedRepositoryRoot, 'remote', 'get-url', 'origin')
        if ($remote -notlike "*$repositorySlug*") {
            throw "MCXA_Lib origin does not correspond to manifest repository '$repositorySlug': $remote"
        }

        $expectedCommit = $Manifest.commit.ToLowerInvariant()
        $head = Get-GitHead -Git $Git -Repository $resolvedRepositoryRoot
        if ($head -ne $expectedCommit) {
            Invoke-Checked -FilePath $Git -Arguments @('-C', $resolvedRepositoryRoot, 'fetch', '--no-tags', 'origin', $expectedCommit)
            Invoke-Checked -FilePath $Git -Arguments @('-C', $resolvedRepositoryRoot, 'checkout', '--detach', $expectedCommit)
            $head = Get-GitHead -Git $Git -Repository $resolvedRepositoryRoot
        }
        if ($head -ne $expectedCommit) {
            throw "MCXA_Lib HEAD mismatch. Expected $expectedCommit, got $head."
        }
        Write-Host "MCXA_Lib commit verified: $head"
    }

    Invoke-WithGitHubToken -Token $Token -Operation $operation
    $repositoryRoot = (Resolve-Path -LiteralPath $repositoryRoot).Path
    $libDir = Join-Path $repositoryRoot $Manifest.subdirectory
    if (-not (Test-Path -LiteralPath $libDir -PathType Container)) {
        throw "MCXA_Lib manifest subdirectory is missing: $libDir"
    }

    return [pscustomobject]@{
        Root = $repositoryRoot
        Lib  = $libDir
    }
}

function Assert-GeneratedRootSafety {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $GeneratedRoot
    )

    $repository = [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\')
    $generated = [IO.Path]::GetFullPath($GeneratedRoot).TrimEnd('\')
    $driveRoot = [IO.Path]::GetPathRoot($generated).TrimEnd('\')
    if ($generated -eq $driveRoot -or $generated.Length -lt 8) {
        throw "Unsafe generated root: $generated"
    }
    if ($generated.Equals($repository, [StringComparison]::OrdinalIgnoreCase) -or
        $generated.StartsWith("$repository\", [StringComparison]::OrdinalIgnoreCase) -or
        $repository.StartsWith("$generated\", [StringComparison]::OrdinalIgnoreCase)) {
        throw "Generated root must be outside and must not contain the repository: $generated"
    }
}

function Invoke-Bootstrap {
    $repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
    $requestedRoot = $Root
    if ([string]::IsNullOrWhiteSpace($requestedRoot)) {
        $requestedRoot = Join-Path (Split-Path -Parent $repositoryRoot) '_ci'
    }
    $ciRoot = [IO.Path]::GetFullPath($requestedRoot)
    Assert-GeneratedRootSafety -RepositoryRoot $repositoryRoot -GeneratedRoot $ciRoot

    if ($Clean -and (Test-Path -LiteralPath $ciRoot)) {
        Write-Stage "Removing generated CI root: $ciRoot"
        Remove-Item -LiteralPath $ciRoot -Recurse -Force
    }

    $downloadsRoot = Join-Path $ciRoot 'downloads'
    $toolsRoot = Join-Path $ciRoot 'tools'
    $venvRoot = Join-Path $ciRoot 'python-venv'
    $workspaceRoot = Join-Path $ciRoot 'zephyr-workspace'
    $dependencyRoot = Join-Path $ciRoot 'dependencies'
    $cacheRoot = Join-Path $ciRoot 'cache'
    $buildRoot = Join-Path $ciRoot 'build'
    $stagingRoot = Join-Path $cacheRoot 'staging'
    foreach ($directory in @($downloadsRoot, $toolsRoot, $dependencyRoot, $cacheRoot, $buildRoot)) {
        New-Item -ItemType Directory -Path $directory -Force | Out-Null
    }

    Write-Stage 'Loading and validating project manifests'
    $toolchainPath = Join-Path $repositoryRoot 'ci\toolchain.json'
    $dependenciesPath = Join-Path $repositoryRoot 'ci\dependencies.json'
    foreach ($path in @($toolchainPath, $dependenciesPath)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required manifest is missing: $path"
        }
    }
    $toolchain = Get-Content -Raw -LiteralPath $toolchainPath | ConvertFrom-Json
    $dependencies = Get-Content -Raw -LiteralPath $dependenciesPath | ConvertFrom-Json
    if ($toolchain.schema_version -ne 1) { throw 'Unsupported toolchain.json schema_version.' }
    if ($toolchain.platform.os -ne 'windows') { throw 'toolchain.json requires a non-Windows platform.' }
    if ($toolchain.platform.architecture -ne 'x86_64') { throw 'toolchain.json requires a non-x86_64 architecture.' }
    if ($dependencies.schema_version -ne 1) { throw 'Unsupported dependencies.json schema_version.' }
    foreach ($property in @('repository', 'commit', 'subdirectory')) {
        [void](Get-RequiredProperty -Object $dependencies.mcxa_lib -Name $property -Context 'dependencies.mcxa_lib')
    }
    if ($dependencies.mcxa_lib.commit -notmatch '^[0-9A-Fa-f]{40}$') {
        throw 'dependencies.mcxa_lib.commit must be an exact 40-character Git commit.'
    }

    $lockRelative = $toolchain.python.requirements_lock.path.Replace('/', [IO.Path]::DirectorySeparatorChar)
    $lockPath = Join-Path $repositoryRoot $lockRelative
    if (-not (Test-Path -LiteralPath $lockPath)) {
        throw "Python requirements lock is missing: $lockPath"
    }
    [void](Assert-FileHash -Path $lockPath -ExpectedSha256 $toolchain.python.requirements_lock.sha256)

    $artifactObjects = @(
        $toolchain.python.artifact,
        $toolchain.cmake.artifact,
        $toolchain.ninja.artifact,
        $toolchain.dtc.package,
        $toolchain.llvm.artifact,
        $toolchain.zephyr_sdk.minimal_bundle,
        $toolchain.zephyr_sdk.target_toolchain
    )
    foreach ($artifact in $artifactObjects) {
        [void](Get-RequiredProperty -Object $artifact -Name 'url' -Context 'artifact')
        [void](Get-RequiredProperty -Object $artifact -Name 'sha256' -Context 'artifact')
        Assert-Sha256Value -Value $artifact.sha256 -Context "Artifact $($artifact.url)"
    }
    foreach ($property in @('filename', 'checksum_type', 'package_type', 'architecture')) {
        [void](Get-RequiredProperty -Object $toolchain.python.artifact -Name $property -Context 'python.artifact')
    }
    if ($toolchain.python.artifact.package_type -ne 'nuget') {
        throw "Unsupported Python package type: $($toolchain.python.artifact.package_type)"
    }
    if ($toolchain.python.artifact.architecture -ne 'x64') {
        throw "Unsupported Python package architecture: $($toolchain.python.artifact.architecture)"
    }
    foreach ($property in @('version', 'artifact', 'clang_tidy_executable')) {
        [void](Get-RequiredProperty -Object $toolchain.llvm -Name $property -Context 'llvm')
    }
    foreach ($property in @('filename', 'checksum_type', 'checksum_source', 'package_type', 'architecture')) {
        [void](Get-RequiredProperty -Object $toolchain.llvm.artifact -Name $property -Context 'llvm.artifact')
    }
    foreach ($payload in $toolchain.dtc.runtime_payloads) {
        foreach ($property in @('filename', 'source', 'sha256')) {
            [void](Get-RequiredProperty -Object $payload -Name $property -Context 'dtc.runtime_payloads')
        }
        if ($payload.source -ne 'embedded_in_package') {
            throw "Unsupported DTC runtime payload source: $($payload.source)"
        }
        Assert-Sha256Value -Value $payload.sha256 -Context "Embedded DTC payload $($payload.filename)"
    }

    Write-Stage 'Resolving required system utilities'
    $gitCommand = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $gitCommand) {
        throw 'git.exe is required on PATH. bootstrap.ps1 does not install Git.'
    }
    $gitExe = $gitCommand.Source
    $gitOutput = Get-CheckedOutput -FilePath $gitExe -Arguments @('--version')
    Write-Host "Git: $gitOutput ($gitExe)"
    $sevenZip = Get-SevenZipPath
    Write-Host "7-Zip: $sevenZip"

    Write-Stage 'Downloading and verifying declared artifacts'
    $pythonPackage = Get-VerifiedDownload -Url $toolchain.python.artifact.url -DestinationPath (Join-Path $downloadsRoot "python\$($toolchain.python.artifact.filename)") -ExpectedSha256 $toolchain.python.artifact.sha256
    $cmakeArchive = Get-VerifiedDownload -Url $toolchain.cmake.artifact.url -DestinationPath (Join-Path $downloadsRoot "cmake\$($toolchain.cmake.artifact.filename)") -ExpectedSha256 $toolchain.cmake.artifact.sha256
    $ninjaArchive = Get-VerifiedDownload -Url $toolchain.ninja.artifact.url -DestinationPath (Join-Path $downloadsRoot "ninja\$($toolchain.ninja.artifact.filename)") -ExpectedSha256 $toolchain.ninja.artifact.sha256
    $dtcPackageName = "$($toolchain.dtc.package.name).$($toolchain.dtc.package.version).nupkg"
    $dtcPackageArchive = Get-VerifiedDownload -Url $toolchain.dtc.package.url -DestinationPath (Join-Path $downloadsRoot "dtc\$dtcPackageName") -ExpectedSha256 $toolchain.dtc.package.sha256
    $llvmPackage = Get-VerifiedDownload -Url $toolchain.llvm.artifact.url -DestinationPath (Join-Path $downloadsRoot "llvm\$($toolchain.llvm.artifact.filename)") -ExpectedSha256 $toolchain.llvm.artifact.sha256
    $sdkMinimalArchive = Get-VerifiedDownload -Url $toolchain.zephyr_sdk.minimal_bundle.url -DestinationPath (Join-Path $downloadsRoot "zephyr-sdk\$($toolchain.zephyr_sdk.minimal_bundle.filename)") -ExpectedSha256 $toolchain.zephyr_sdk.minimal_bundle.sha256
    $sdkToolchainArchive = Get-VerifiedDownload -Url $toolchain.zephyr_sdk.target_toolchain.url -DestinationPath (Join-Path $downloadsRoot "zephyr-sdk\$($toolchain.zephyr_sdk.target_toolchain.filename)") -ExpectedSha256 $toolchain.zephyr_sdk.target_toolchain.sha256

    Write-Stage 'Installing isolated build tools'
    $pythonRoot = Join-Path $toolsRoot "python\$($toolchain.python.version)"
    $basePython = Install-PythonFromNuGetPackage -Manifest $toolchain.python -Package $pythonPackage -PythonRoot $pythonRoot -StagingRoot $stagingRoot
    $venvPython = Initialize-PythonEnvironment -Toolchain $toolchain -RepositoryRoot $repositoryRoot -PythonExe $basePython -VenvRoot $venvRoot
    $cmakeExe = Install-CMake -Manifest $toolchain.cmake -Archive $cmakeArchive -ToolsRoot $toolsRoot -StagingRoot $stagingRoot
    $ninjaExe = Install-Ninja -Manifest $toolchain.ninja -Archive $ninjaArchive -ToolsRoot $toolsRoot -StagingRoot $stagingRoot
    $dtcExe = Install-DtcRuntime -Manifest $toolchain.dtc -PackageArchive $dtcPackageArchive -ToolsRoot $toolsRoot -StagingRoot $stagingRoot -SevenZip $sevenZip
    $clangTidyExe = Install-ClangTidy -Manifest $toolchain.llvm -Package $llvmPackage -ToolsRoot $toolsRoot -StagingRoot $stagingRoot -SevenZip $sevenZip
    $sdk = Install-ZephyrSdk -Manifest $toolchain.zephyr_sdk -MinimalArchive $sdkMinimalArchive -ToolchainArchive $sdkToolchainArchive -ToolsRoot $toolsRoot -StagingRoot $stagingRoot -SevenZip $sevenZip

    Write-Stage 'Validating scalar tool paths for process-local PATH'
    $basePython = Assert-SinglePath -Value $basePython -Name 'Base Python' -PathType Leaf
    $venvPython = Assert-SinglePath -Value $venvPython -Name 'Venv Python' -PathType Leaf
    $cmakeExe = Assert-SinglePath -Value $cmakeExe -Name 'CMake' -PathType Leaf
    $ninjaExe = Assert-SinglePath -Value $ninjaExe -Name 'Ninja' -PathType Leaf
    $dtcExe = Assert-SinglePath -Value $dtcExe -Name 'DTC' -PathType Leaf
    $clangTidyExe = Assert-SinglePath -Value $clangTidyExe -Name 'clang-tidy' -PathType Leaf
    $gitExe = Assert-SinglePath -Value $gitExe -Name 'Git' -PathType Leaf

    $sdkValues = @($sdk)
    if ($sdkValues.Count -ne 1 -or $null -eq $sdkValues[0]) {
        throw "Zephyr SDK result must contain exactly one object; found $($sdkValues.Count). This may indicate success-stream contamination."
    }
    $sdkRoot = Assert-SinglePath -Value $sdkValues[0].Root -Name 'Zephyr SDK root' -PathType Container
    $sdkGcc = Assert-SinglePath -Value $sdkValues[0].Gcc -Name 'Zephyr SDK GCC' -PathType Leaf
    $sdk = [pscustomobject]@{
        Root = $sdkRoot
        Gcc  = $sdkGcc
    }

    $originalPath = $env:PATH
    try {
        $pathParts = @(
            (Split-Path -Parent $venvPython),
            (Split-Path -Parent $cmakeExe),
            (Split-Path -Parent $ninjaExe),
            (Split-Path -Parent $dtcExe),
            (Split-Path -Parent $clangTidyExe),
            (Split-Path -Parent $sdk.Gcc),
            (Split-Path -Parent $gitExe),
            "$env:SystemRoot\System32",
            $env:SystemRoot,
            "$env:SystemRoot\System32\Wbem"
        )
        $env:PATH = ($pathParts | Select-Object -Unique) -join ';'

        Write-Stage 'Creating or validating the Zephyr west workspace'
        $zephyrBase = Initialize-ZephyrWorkspace -Manifest $toolchain.zephyr -VenvPython $venvPython -Git $gitExe -Workspace $workspaceRoot

        Write-Stage 'Creating or validating MCXA_Lib'
        $mcxaLib = Initialize-McxaLib -Manifest $dependencies.mcxa_lib -Git $gitExe -DependencyRoot $dependencyRoot -Token $GitHubToken

        Write-Stage 'Performing final bootstrap validation'
        $pythonOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('--version')
        Assert-VersionOutput -Output $pythonOutput -ExpectedVersion $toolchain.python.version -Component 'Python'
        $pipOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('-m', 'pip', '--version')
        Assert-VersionOutput -Output $pipOutput -ExpectedVersion $toolchain.python.pip_version -Component 'pip'
        $westOutput = Get-CheckedOutput -FilePath $venvPython -Arguments @('-m', 'west', '--version')
        Assert-VersionOutput -Output $westOutput -ExpectedVersion $toolchain.zephyr.west_version -Component 'west'
        Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $cmakeExe -Arguments @('--version')) -ExpectedVersion $toolchain.cmake.version -Component 'CMake'
        Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $ninjaExe -Arguments @('--version')) -ExpectedVersion $toolchain.ninja.version -Component 'Ninja'
        Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $dtcExe -Arguments @('--version')) -ExpectedVersion $toolchain.dtc.version -Component 'DTC'
        Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $clangTidyExe -Arguments @('--version')) -ExpectedVersion $toolchain.llvm.version -Component 'clang-tidy'
        Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $sdk.Gcc -Arguments @('--version')) -ExpectedVersion $toolchain.zephyr_sdk.target_toolchain.gcc_version -Component 'GCC'
        if ((Get-Content -Raw -LiteralPath (Join-Path $sdk.Root 'sdk_version')).Trim() -ne $toolchain.zephyr_sdk.version) {
            throw 'Zephyr SDK final version validation failed.'
        }
        if ((Get-GitHead -Git $gitExe -Repository $zephyrBase) -ne $toolchain.zephyr.commit.ToLowerInvariant()) {
            throw 'Zephyr final commit validation failed.'
        }
        if ((Get-GitHead -Git $gitExe -Repository $mcxaLib.Root) -ne $dependencies.mcxa_lib.commit.ToLowerInvariant()) {
            throw 'MCXA_Lib final commit validation failed.'
        }

        $generatedPaths = @($ciRoot, $basePython, $venvRoot, $venvPython, $cmakeExe, $ninjaExe, $dtcExe, $clangTidyExe, $sdk.Root, $workspaceRoot, $zephyrBase, $mcxaLib.Root, $mcxaLib.Lib, $cacheRoot, $buildRoot)
        $rootPrefix = "$($ciRoot.TrimEnd('\'))\"
        foreach ($path in $generatedPaths) {
            $fullPath = [IO.Path]::GetFullPath($path)
            if (-not ($fullPath.Equals($ciRoot, [StringComparison]::OrdinalIgnoreCase) -or $fullPath.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase))) {
                throw "Generated dependency/tool path is outside the CI root: $fullPath"
            }
            if (-not [string]::IsNullOrWhiteSpace($env:USERPROFILE) -and $fullPath.StartsWith($env:USERPROFILE, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Generated dependency/tool path depends on the user profile: $fullPath"
            }
        }

        $environmentPath = Join-Path $ciRoot 'bootstrap-environment.json'
        $environment = [ordered]@{
            schema_version      = 1
            repository_root    = $repositoryRoot
            ci_root            = $ciRoot
            python             = $venvPython
            python_venv        = $venvRoot
            python_base        = $basePython
            cmake              = $cmakeExe
            ninja              = $ninjaExe
            dtc                = $dtcExe
            clang_tidy         = $clangTidyExe
            zephyr_sdk_root    = $sdk.Root
            zephyr_workspace   = $workspaceRoot
            zephyr_base        = $zephyrBase
            mcxa_lib_root      = $mcxaLib.Root
            mcxa_lib_dir       = $mcxaLib.Lib
            cache_root         = $cacheRoot
            build_root         = $buildRoot
            git                = $gitExe
            verified_versions  = [ordered]@{
                python         = $toolchain.python.version
                pip            = $toolchain.python.pip_version
                west           = $toolchain.zephyr.west_version
                cmake          = $toolchain.cmake.version
                ninja          = $toolchain.ninja.version
                dtc            = $toolchain.dtc.version
                clang_tidy     = $toolchain.llvm.version
                zephyr_sdk     = $toolchain.zephyr_sdk.version
                gcc            = $toolchain.zephyr_sdk.target_toolchain.gcc_version
                binutils       = $toolchain.zephyr_sdk.target_toolchain.binutils_version
                zephyr_commit  = $toolchain.zephyr.commit
                mcxa_lib_commit = $dependencies.mcxa_lib.commit
                git_tested     = $toolchain.git.tested_version
                git_actual     = $gitOutput
            }
            verified_downloads = $script:VerifiedDownloads.Count
        }
        $json = $environment | ConvertTo-Json -Depth 8
        [IO.File]::WriteAllText($environmentPath, $json, [Text.UTF8Encoding]::new($false))

        Write-Stage 'Bootstrap completed successfully'
        Write-Host "Repository root : $repositoryRoot"
        Write-Host "CI root         : $ciRoot"
        Write-Host "Python          : $venvPython"
        Write-Host "Python venv     : $venvRoot"
        Write-Host "CMake           : $cmakeExe"
        Write-Host "Ninja           : $ninjaExe"
        Write-Host "DTC             : $dtcExe"
        Write-Host "clang-tidy      : $clangTidyExe"
        Write-Host "Zephyr SDK      : $($sdk.Root)"
        Write-Host "Zephyr workspace: $workspaceRoot"
        Write-Host "Zephyr base     : $zephyrBase"
        Write-Host "MCXA_Lib root   : $($mcxaLib.Root)"
        Write-Host "MCXA_Lib Lib    : $($mcxaLib.Lib)"
        Write-Host "Git             : $gitExe"
        Write-Host "Environment file: $environmentPath"
    }
    finally {
        $env:PATH = $originalPath
    }
}

try {
    Invoke-Bootstrap
}
catch {
    Write-Host "BOOTSTRAP FAILED: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
