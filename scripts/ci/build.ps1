[CmdletBinding()]
param(
    [string] $EnvironmentFile,
    [switch] $Clean,
    [switch] $GenerateEphemeralSigningKey,
    [switch] $RequireBaseline
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

function Assert-SinglePath {
    param(
        [Parameter(Mandatory)][AllowNull()][object] $Value,
        [Parameter(Mandatory)][string] $Name,
        [Parameter(Mandatory)][ValidateSet('Leaf', 'Container')][string] $PathType
    )

    $values = @($Value)
    if ($values.Count -ne 1) {
        throw "$Name must contain exactly one path value; found $($values.Count)."
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

function Get-NormalizedPath {
    param([Parameter(Mandatory)][string] $Path)

    return [IO.Path]::GetFullPath($Path).TrimEnd('\')
}

function Assert-SamePath {
    param(
        [Parameter(Mandatory)][string] $Actual,
        [Parameter(Mandatory)][string] $Expected,
        [Parameter(Mandatory)][string] $Description
    )

    $actualFull = Get-NormalizedPath -Path $Actual
    $expectedFull = Get-NormalizedPath -Path $Expected
    if (-not $actualFull.Equals($expectedFull, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Description path mismatch. Expected '$expectedFull', got '$actualFull'."
    }
}

function Assert-SafeChildPath {
    param(
        [Parameter(Mandatory)][string] $Root,
        [Parameter(Mandatory)][string] $Target,
        [Parameter(Mandatory)][string] $Description
    )

    $rootFull = (Get-NormalizedPath -Path $Root)
    $targetFull = (Get-NormalizedPath -Path $Target)
    $rootPrefix = "$rootFull\"
    if ($targetFull.Equals($rootFull, [StringComparison]::OrdinalIgnoreCase) -or
        -not $targetFull.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe $Description path outside its generated root: $targetFull"
    }

    return $targetFull
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

function Invoke-CheckedCapture {
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

    return [pscustomobject]@{
        Lines = $output
        Text  = ($output -join "`n")
    }
}

function Get-CheckedOutput {
    param(
        [Parameter(Mandatory)][string] $FilePath,
        [Parameter()][string[]] $Arguments = @()
    )

    $output = @(& $FilePath @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed with exit code $exitCode`: $FilePath $($Arguments -join ' ')`n$($output -join "`n")"
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

function Get-GitHead {
    param(
        [Parameter(Mandatory)][string] $Git,
        [Parameter(Mandatory)][string] $Repository
    )

    return (Get-CheckedOutput -FilePath $Git -Arguments @('-C', $Repository, 'rev-parse', 'HEAD')).ToLowerInvariant()
}

function Assert-EnvironmentVersions {
    param(
        [Parameter(Mandatory)][object] $Environment,
        [Parameter(Mandatory)][object] $Toolchain,
        [Parameter(Mandatory)][object] $Dependencies
    )

    $versions = Get-RequiredProperty -Object $Environment -Name 'verified_versions' -Context 'bootstrap environment'
    $expected = [ordered]@{
        python          = $Toolchain.python.version
        pip             = $Toolchain.python.pip_version
        west            = $Toolchain.zephyr.west_version
        cmake           = $Toolchain.cmake.version
        ninja           = $Toolchain.ninja.version
        dtc             = $Toolchain.dtc.version
        zephyr_sdk      = $Toolchain.zephyr_sdk.version
        gcc             = $Toolchain.zephyr_sdk.target_toolchain.gcc_version
        binutils        = $Toolchain.zephyr_sdk.target_toolchain.binutils_version
        zephyr_commit   = $Toolchain.zephyr.commit.ToLowerInvariant()
        mcxa_lib_commit = $Dependencies.mcxa_lib.commit.ToLowerInvariant()
    }

    foreach ($name in $expected.Keys) {
        $actual = [string](Get-RequiredProperty -Object $versions -Name $name -Context 'bootstrap environment.verified_versions')
        if (-not $actual.Equals([string]$expected[$name], [StringComparison]::OrdinalIgnoreCase)) {
            throw "Bootstrap environment version '$name' is stale. Expected '$($expected[$name])', got '$actual'. Rerun bootstrap.ps1."
        }
    }
}

function Get-TrackedApplicationFiles {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $Git
    )

    $gitMetadata = Join-Path $RepositoryRoot '.git'
    $hgMetadata = Join-Path $RepositoryRoot '.hg'
    $isGitCheckout = (
        (Test-Path -LiteralPath $gitMetadata -PathType Leaf) -or
        ((Test-Path -LiteralPath $gitMetadata -PathType Container) -and
         (Test-Path -LiteralPath (Join-Path $gitMetadata 'HEAD') -PathType Leaf))
    )
    $isMercurialCheckout = Test-Path -LiteralPath $hgMetadata -PathType Container

    if ($isGitCheckout) {
        $output = Get-CheckedOutput -FilePath $Git -Arguments @('-C', $RepositoryRoot, 'ls-files', '--', 'hello_world')
    }
    elseif ($isMercurialCheckout) {
        $hgCommand = Get-Command 'hg.exe' -ErrorAction SilentlyContinue
        if ($null -eq $hgCommand) {
            $hgCommand = Get-Command 'hg' -ErrorAction SilentlyContinue
        }
        if ($null -eq $hgCommand) {
            throw 'Mercurial is required to enumerate tracked application files for the ephemeral source copy.'
        }
        $output = Get-CheckedOutput -FilePath $hgCommand.Source -Arguments @('--cwd', $RepositoryRoot, 'files', 'hello_world')
    }
    else {
        throw 'Repository is neither a Git nor Mercurial checkout.'
    }

    $files = @($output -split '\r?\n' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($files.Count -eq 0) {
        throw 'Source control reported no tracked files beneath hello_world.'
    }
    return $files
}

function Copy-TrackedApplicationSource {
    param(
        [Parameter(Mandatory)][string] $RepositoryRoot,
        [Parameter(Mandatory)][string] $DestinationRoot,
        [Parameter(Mandatory)][string] $Git
    )

    New-Item -ItemType Directory -Path $DestinationRoot -Force | Out-Null
    $privateKeyRelative = 'hello_world\keys\wallcontroller_signingkey.pem'
    $repositoryPrefix = "$(Get-NormalizedPath -Path $RepositoryRoot)\"
    $destinationPrefix = "$(Get-NormalizedPath -Path $DestinationRoot)\"
    $copied = 0

    foreach ($sourceControlPath in @(Get-TrackedApplicationFiles -RepositoryRoot $RepositoryRoot -Git $Git)) {
        $relative = ([string]$sourceControlPath).Replace('/', '\').TrimStart('\')
        if ($relative.Equals($privateKeyRelative, [StringComparison]::OrdinalIgnoreCase)) {
            continue
        }

        $source = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $relative))
        $destination = [IO.Path]::GetFullPath((Join-Path $DestinationRoot $relative))
        if (-not $source.StartsWith($repositoryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Tracked source path escapes the repository: $relative"
        }
        if (-not $destination.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Tracked destination path escapes the generated source root: $relative"
        }
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Tracked application file is missing: $source"
        }

        $destinationDirectory = Split-Path -Parent $destination
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
        $copied++
    }

    $applicationSource = Join-Path $DestinationRoot 'hello_world'
    if (-not (Test-Path -LiteralPath (Join-Path $applicationSource 'CMakeLists.txt') -PathType Leaf)) {
        throw "Generated application copy is incomplete: $applicationSource"
    }
    Write-Host "Copied $copied tracked application files without ignored/private files."
    return $applicationSource
}

function New-EphemeralSigningKey {
    param(
        [Parameter(Mandatory)][string] $Python,
        [Parameter(Mandatory)][string] $Destination
    )

    $destinationDirectory = Split-Path -Parent $Destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
    $code = @'
import sys
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
data = key.private_bytes(
    encoding=serialization.Encoding.PEM,
    format=serialization.PrivateFormat.PKCS8,
    encryption_algorithm=serialization.NoEncryption(),
)
with open(sys.argv[1], "wb") as output:
    output.write(data)
'@

    Write-Host 'Generating disposable RSA-2048 signing key in the CI build source.'
    $output = @(& $Python '-c' $code $Destination 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Disposable signing-key generation failed with exit code $exitCode`: $($output -join "`n")"
    }
    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        throw 'Disposable signing-key generation did not create the expected file.'
    }
}

function Assert-GeneratedBuildPaths {
    param(
        [Parameter(Mandatory)][string] $BuildDirectory,
        [Parameter(Mandatory)][object] $Environment,
        [Parameter(Mandatory)][string] $ApplicationSource,
        [Parameter()][string] $EphemeralKey
    )

    $files = @(
        (Join-Path $BuildDirectory 'CMakeCache.txt'),
        (Join-Path $BuildDirectory 'build.ninja'),
        (Join-Path $BuildDirectory 'CMakeFiles\rules.ninja'),
        (Join-Path $BuildDirectory 'compile_commands.json')
    )
    foreach ($file in $files) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
            throw "Required configured build file is missing: $file"
        }
    }

    $text = (($files | ForEach-Object { Get-Content -Raw -LiteralPath $_ }) -join "`n").Replace('\', '/').ToLowerInvariant()
    $forbidden = @(
        'C:/Users/sameera.SONAVOX',
        'C:/SCODE/CI_Test/CI_Tools',
        'C:/SCODE/CI_Test/ZephyrWorkspace',
        'C:/SCODE/CI_Test/NXP_MCXA_LIB',
        'C:/SCODE/NXP_Dev/MCXA156/zephyr_440_mcxa156',
        'C:/SCODE/NXP_Dev/MCXA_Lib/Lib'
    )
    foreach ($path in $forbidden) {
        if ($text.Contains($path.ToLowerInvariant())) {
            throw "Configured build contains a forbidden legacy/developer path: $path"
        }
    }

    $requiredPaths = @(
        $Environment.cmake,
        $Environment.ninja,
        $Environment.python,
        $Environment.dtc,
        $Environment.zephyr_base,
        $Environment.mcxa_lib_dir,
        $Environment.zephyr_sdk_root,
        $ApplicationSource
    )
    foreach ($path in $requiredPaths) {
        $normalized = ([IO.Path]::GetFullPath([string]$path)).Replace('\', '/').ToLowerInvariant()
        if (-not $text.Contains($normalized)) {
            throw "Configured build does not reference the prepared path: $path"
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($EphemeralKey)) {
        $normalizedKey = ([IO.Path]::GetFullPath($EphemeralKey)).Replace('\', '/').ToLowerInvariant()
        if (-not $text.Contains($normalizedKey)) {
            throw 'Configured signing commands do not reference the disposable CI signing key.'
        }
        $developmentKey = ([IO.Path]::GetFullPath((Join-Path $Environment.repository_root 'hello_world\keys\wallcontroller_signingkey.pem'))).Replace('\', '/').ToLowerInvariant()
        if ($text.Contains($developmentKey)) {
            throw 'Configured signing commands reference the development signing key.'
        }
    }

    Write-Host 'Configured build path isolation verified.'
}

function Convert-ReportedSizeToBytes {
    param(
        [Parameter(Mandatory)][string] $Value,
        [Parameter(Mandatory)][string] $Unit
    )

    $number = [double]($Value.Replace(',', ''))
    $multiplier = switch ($Unit.ToUpperInvariant()) {
        'B'  { 1 }
        'KB' { 1024 }
        'MB' { 1024 * 1024 }
        'GB' { 1024 * 1024 * 1024 }
        default { throw "Unsupported memory-size unit: $Unit" }
    }
    return [int64][Math]::Round($number * $multiplier)
}

function Get-MemoryUsage {
    param([Parameter(Mandatory)][string] $BuildOutput)

    $flash = [regex]::Match($BuildOutput, '(?im)^\s*FLASH:\s*(?<used>[0-9,]+)\s+B\s+(?<capacity>[0-9,.]+)\s*(?<unit>[KMG]?B)')
    $ram = [regex]::Match($BuildOutput, '(?im)^\s*(?:RAM|SRAM):\s*(?<used>[0-9,]+)\s+B\s+(?<capacity>[0-9,.]+)\s*(?<unit>[KMG]?B)')
    if (-not $flash.Success -or -not $ram.Success) {
        throw 'Could not parse FLASH and RAM usage from the build output.'
    }

    return [pscustomobject]@{
        FlashUsedBytes     = [int64]($flash.Groups['used'].Value.Replace(',', ''))
        FlashCapacityBytes = Convert-ReportedSizeToBytes -Value $flash.Groups['capacity'].Value -Unit $flash.Groups['unit'].Value
        RamUsedBytes       = [int64]($ram.Groups['used'].Value.Replace(',', ''))
        RamCapacityBytes   = Convert-ReportedSizeToBytes -Value $ram.Groups['capacity'].Value -Unit $ram.Groups['unit'].Value
    }
}

function Get-BuildVersion {
    param([Parameter(Mandatory)][string] $BuildDirectory)

    $versionHeader = Join-Path $BuildDirectory 'zephyr\include\generated\zephyr\version.h'
    if (-not (Test-Path -LiteralPath $versionHeader -PathType Leaf)) {
        throw "Generated Zephyr version header is missing: $versionHeader"
    }
    $match = [regex]::Match((Get-Content -Raw -LiteralPath $versionHeader), '(?m)^#define\s+BUILD_VERSION\s+(?<version>\S+)\s*$')
    if (-not $match.Success -or [string]::IsNullOrWhiteSpace($match.Groups['version'].Value)) {
        throw 'Generated BUILD_VERSION is missing or empty.'
    }
    return $match.Groups['version'].Value.Trim('"')
}

function Invoke-FirmwareBuild {
    $repositoryRoot = Get-NormalizedPath -Path (Join-Path $PSScriptRoot '..\..')
    if ([string]::IsNullOrWhiteSpace($EnvironmentFile)) {
        $EnvironmentFile = Join-Path (Split-Path -Parent $repositoryRoot) '_ci\bootstrap-environment.json'
    }
    $environmentPath = [IO.Path]::GetFullPath($EnvironmentFile)
    if (-not (Test-Path -LiteralPath $environmentPath -PathType Leaf)) {
        throw "Bootstrap environment file does not exist: $environmentPath"
    }

    Write-Stage 'Loading and validating the prepared environment'
    $environment = Get-Content -Raw -LiteralPath $environmentPath | ConvertFrom-Json
    $toolchainPath = Join-Path $repositoryRoot 'ci\toolchain.json'
    $dependenciesPath = Join-Path $repositoryRoot 'ci\dependencies.json'
    $toolchain = Get-Content -Raw -LiteralPath $toolchainPath | ConvertFrom-Json
    $dependencies = Get-Content -Raw -LiteralPath $dependenciesPath | ConvertFrom-Json

    foreach ($name in @('repository_root', 'ci_root', 'python', 'python_venv', 'cmake', 'ninja', 'dtc', 'zephyr_sdk_root', 'zephyr_workspace', 'zephyr_base', 'mcxa_lib_root', 'mcxa_lib_dir', 'cache_root', 'build_root')) {
        [void](Get-RequiredProperty -Object $environment -Name $name -Context 'bootstrap environment')
    }

    $environment.repository_root = Assert-SinglePath -Value $environment.repository_root -Name 'Repository root' -PathType Container
    Assert-SamePath -Actual $environment.repository_root -Expected $repositoryRoot -Description 'Bootstrap environment repository'
    $environment.ci_root = Assert-SinglePath -Value $environment.ci_root -Name 'CI root' -PathType Container
    $environment.python = Assert-SinglePath -Value $environment.python -Name 'Python' -PathType Leaf
    $environment.python_venv = Assert-SinglePath -Value $environment.python_venv -Name 'Python venv' -PathType Container
    $environment.cmake = Assert-SinglePath -Value $environment.cmake -Name 'CMake' -PathType Leaf
    $environment.ninja = Assert-SinglePath -Value $environment.ninja -Name 'Ninja' -PathType Leaf
    $environment.dtc = Assert-SinglePath -Value $environment.dtc -Name 'DTC' -PathType Leaf
    $environment.zephyr_sdk_root = Assert-SinglePath -Value $environment.zephyr_sdk_root -Name 'Zephyr SDK root' -PathType Container
    $environment.zephyr_workspace = Assert-SinglePath -Value $environment.zephyr_workspace -Name 'Zephyr workspace' -PathType Container
    $environment.zephyr_base = Assert-SinglePath -Value $environment.zephyr_base -Name 'Zephyr base' -PathType Container
    $environment.mcxa_lib_root = Assert-SinglePath -Value $environment.mcxa_lib_root -Name 'MCXA_Lib root' -PathType Container
    $environment.mcxa_lib_dir = Assert-SinglePath -Value $environment.mcxa_lib_dir -Name 'MCXA_Lib directory' -PathType Container
    $environment.cache_root = Assert-SinglePath -Value $environment.cache_root -Name 'Cache root' -PathType Container
    $environment.build_root = Assert-SinglePath -Value $environment.build_root -Name 'Build root' -PathType Container

    $gitValue = Get-RequiredProperty -Object $environment -Name 'git' -Context 'bootstrap environment'
    $gitExe = Assert-SinglePath -Value $gitValue -Name 'Git' -PathType Leaf
    $gccExe = Assert-SinglePath -Value (Join-Path $environment.zephyr_sdk_root 'gnu\arm-zephyr-eabi\bin\arm-zephyr-eabi-gcc.exe') -Name 'ARM GCC' -PathType Leaf
    $sdkVersionFile = Assert-SinglePath -Value (Join-Path $environment.zephyr_sdk_root 'sdk_version') -Name 'Zephyr SDK version file' -PathType Leaf

    Assert-EnvironmentVersions -Environment $environment -Toolchain $toolchain -Dependencies $dependencies

    Write-Stage 'Verifying prepared tool versions'
    Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $environment.python -Arguments @('--version')) -ExpectedVersion $toolchain.python.version -Component 'Python'
    Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $environment.cmake -Arguments @('--version')) -ExpectedVersion $toolchain.cmake.version -Component 'CMake'
    Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $environment.ninja -Arguments @('--version')) -ExpectedVersion $toolchain.ninja.version -Component 'Ninja'
    Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $environment.dtc -Arguments @('--version')) -ExpectedVersion $toolchain.dtc.version -Component 'DTC'
    Assert-VersionOutput -Output (Get-CheckedOutput -FilePath $gccExe -Arguments @('--version')) -ExpectedVersion $toolchain.zephyr_sdk.target_toolchain.gcc_version -Component 'GCC'
    $sdkVersion = (Get-Content -Raw -LiteralPath $sdkVersionFile).Trim()
    if ($sdkVersion -ne $toolchain.zephyr_sdk.version) {
        throw "Zephyr SDK version mismatch. Expected $($toolchain.zephyr_sdk.version), got $sdkVersion."
    }
    Write-Host "Zephyr SDK version verified: $sdkVersion"

    Write-Stage 'Verifying prepared source revisions and board support'
    $zephyrCommit = Get-GitHead -Git $gitExe -Repository $environment.zephyr_base
    $expectedZephyrCommit = $toolchain.zephyr.commit.ToLowerInvariant()
    if ($zephyrCommit -ne $expectedZephyrCommit) {
        throw "Zephyr revision mismatch. Expected $expectedZephyrCommit, got $zephyrCommit. Rerun bootstrap.ps1."
    }
    $mcxaLibCommit = Get-GitHead -Git $gitExe -Repository $environment.mcxa_lib_root
    $expectedMcxaLibCommit = $dependencies.mcxa_lib.commit.ToLowerInvariant()
    if ($mcxaLibCommit -ne $expectedMcxaLibCommit) {
        throw "MCXA_Lib revision mismatch. Expected $expectedMcxaLibCommit, got $mcxaLibCommit. Rerun bootstrap.ps1."
    }
    Assert-SamePath -Actual $environment.mcxa_lib_dir -Expected (Join-Path $environment.mcxa_lib_root $dependencies.mcxa_lib.subdirectory) -Description 'MCXA_Lib subdirectory'
    [void](Assert-SinglePath -Value (Join-Path $environment.zephyr_workspace 'modules\hal\nxp\mcux\mcux-sdk-ng') -Name 'Zephyr MCUX SDK NG' -PathType Container)
    [void](Assert-SinglePath -Value (Join-Path $environment.zephyr_base 'boards\nxp\frdm_mcxa156') -Name 'MCXA156 board support' -PathType Container)
    Write-Host "Zephyr commit verified: $zephyrCommit"
    Write-Host "MCXA_Lib commit verified: $mcxaLibCommit"

    $buildDirectory = Assert-SafeChildPath -Root $environment.build_root -Target (Join-Path $environment.build_root 'wallcontroller') -Description 'firmware build directory'
    if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
        Write-Stage "Removing generated firmware build directory: $buildDirectory"
        Remove-Item -LiteralPath $buildDirectory -Recurse -Force
    }
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

    $canonicalApplication = Join-Path $repositoryRoot 'hello_world'
    $developmentKey = Join-Path $canonicalApplication 'keys\wallcontroller_signingkey.pem'
    $applicationSource = $canonicalApplication
    $ephemeralKey = $null
    if ($GenerateEphemeralSigningKey) {
        if (Test-Path -LiteralPath $developmentKey -PathType Leaf) {
            Write-Host 'A development signing key exists and will not be read or copied.'
        }
        $generatedSourceRoot = Assert-SafeChildPath -Root $buildDirectory -Target (Join-Path $buildDirectory 'application-build-source') -Description 'generated application source'
        $applicationSource = Copy-TrackedApplicationSource -RepositoryRoot $repositoryRoot -DestinationRoot $generatedSourceRoot -Git $gitExe
        $ephemeralKey = Join-Path $applicationSource 'keys\wallcontroller_signingkey.pem'
        New-EphemeralSigningKey -Python $environment.python -Destination $ephemeralKey
    }
    else {
        $signingConfig = Get-Content -Raw -LiteralPath (Join-Path $canonicalApplication 'prj.conf')
        if ($signingConfig -match '(?m)^\s*CONFIG_MCUBOOT_SIGNATURE_KEY_FILE\s*=') {
            throw 'The application requires a signing key. Normal CI refuses to read a repository/development private key; use -GenerateEphemeralSigningKey.'
        }
    }

    $capabilityCache = Join-Path $environment.cache_root 'WallControllerToolchainCapabilityDatabase'
    New-Item -ItemType Directory -Path $capabilityCache -Force | Out-Null

    $pathParts = @(
        (Split-Path -Parent $environment.python),
        (Split-Path -Parent $environment.cmake),
        (Split-Path -Parent $environment.ninja),
        (Split-Path -Parent $environment.dtc),
        (Split-Path -Parent $gccExe),
        (Split-Path -Parent $gitExe),
        "$env:SystemRoot\System32",
        $env:SystemRoot,
        "$env:SystemRoot\System32\Wbem"
    )

    $savedEnvironment = @{}
    foreach ($name in @('PATH', 'ZEPHYR_BASE', 'ZEPHYR_SDK_INSTALL_DIR', 'ZEPHYR_TOOLCHAIN_VARIANT')) {
        $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    }

    try {
        $env:PATH = ($pathParts | Select-Object -Unique) -join ';'
        $env:ZEPHYR_BASE = $environment.zephyr_base
        $env:ZEPHYR_SDK_INSTALL_DIR = $environment.zephyr_sdk_root
        $env:ZEPHYR_TOOLCHAIN_VARIANT = 'zephyr'

        Write-Stage 'Configuring the firmware'
        $configureArguments = @(
            '-S', $applicationSource,
            '-B', $buildDirectory,
            '-G', 'Ninja',
            "-DBOARD:STRING=$($toolchain.validation.board)",
            "-DCMAKE_BUILD_TYPE:STRING=$($toolchain.validation.build_type)",
            '-DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON',
            "-DCMAKE_MAKE_PROGRAM:FILEPATH=$($environment.ninja)",
            "-DPython3_EXECUTABLE:FILEPATH=$($environment.python)",
            "-DDTC:FILEPATH=$($environment.dtc)",
            "-DZEPHYR_BASE:PATH=$($environment.zephyr_base)",
            "-DMCXA_LIB_DIR:PATH=$($environment.mcxa_lib_dir)",
            "-DZEPHYR_SDK_INSTALL_DIR:PATH=$($environment.zephyr_sdk_root)",
            '-DZEPHYR_TOOLCHAIN_VARIANT:STRING=zephyr',
            "-DZEPHYR_TOOLCHAIN_CAPABILITY_CACHE_DIR:PATH=$capabilityCache"
        )
        Invoke-Checked -FilePath $environment.cmake -Arguments $configureArguments

        Write-Stage 'Validating configured paths'
        Assert-GeneratedBuildPaths -BuildDirectory $buildDirectory -Environment $environment -ApplicationSource $applicationSource -EphemeralKey $ephemeralKey

        Write-Stage 'Building the firmware'
        $buildResult = Invoke-CheckedCapture -FilePath $environment.cmake -Arguments @('--build', $buildDirectory)
    }
    finally {
        foreach ($name in $savedEnvironment.Keys) {
            [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
        }
    }

    Write-Stage 'Validating firmware outputs'
    $artifactPaths = [ordered]@{
        elf        = Join-Path $buildDirectory 'zephyr\zephyr.elf'
        bin        = Join-Path $buildDirectory 'zephyr\zephyr.bin'
        hex        = Join-Path $buildDirectory 'zephyr\zephyr.hex'
        signed_bin = Join-Path $buildDirectory 'zephyr\zephyr.signed.bin'
        signed_hex = Join-Path $buildDirectory 'zephyr\zephyr.signed.hex'
    }
    foreach ($name in @('elf', 'bin', 'hex')) {
        if (-not (Test-Path -LiteralPath $artifactPaths[$name] -PathType Leaf)) {
            throw "Required firmware artifact is missing: $($artifactPaths[$name])"
        }
    }
    if ($GenerateEphemeralSigningKey) {
        foreach ($name in @('signed_bin', 'signed_hex')) {
            if (-not (Test-Path -LiteralPath $artifactPaths[$name] -PathType Leaf)) {
                throw "Required signed firmware artifact is missing: $($artifactPaths[$name])"
            }
        }
    }

    $memory = Get-MemoryUsage -BuildOutput $buildResult.Text
    $unsignedBinHash = (Get-FileHash -LiteralPath $artifactPaths.bin -Algorithm SHA256).Hash
    $unsignedHexHash = (Get-FileHash -LiteralPath $artifactPaths.hex -Algorithm SHA256).Hash
    $buildVersion = Get-BuildVersion -BuildDirectory $buildDirectory
    if (-not $buildVersion.Equals([string]$toolchain.zephyr.release, [StringComparison]::OrdinalIgnoreCase)) {
        throw "BUILD_VERSION mismatch. Expected $($toolchain.zephyr.release), got $buildVersion."
    }

    $memoryMatches = (
        $memory.FlashUsedBytes -eq [int64]$toolchain.validation.flash_used_bytes -and
        $memory.FlashCapacityBytes -eq [int64]$toolchain.validation.flash_capacity_bytes -and
        $memory.RamUsedBytes -eq [int64]$toolchain.validation.ram_used_bytes -and
        $memory.RamCapacityBytes -eq [int64]$toolchain.validation.ram_capacity_bytes
    )
    $hashesMatch = (
        $unsignedBinHash.Equals([string]$toolchain.validation.unsigned_bin_sha256, [StringComparison]::OrdinalIgnoreCase) -and
        $unsignedHexHash.Equals([string]$toolchain.validation.unsigned_hex_sha256, [StringComparison]::OrdinalIgnoreCase)
    )
    $baselineMatch = $memoryMatches -and $hashesMatch
    if ($RequireBaseline -and -not $memoryMatches) {
        throw "Memory baseline mismatch. FLASH $($memory.FlashUsedBytes)/$($memory.FlashCapacityBytes), RAM $($memory.RamUsedBytes)/$($memory.RamCapacityBytes)."
    }
    if ($RequireBaseline -and -not $hashesMatch) {
        throw "Unsigned firmware hash baseline mismatch. BIN $unsignedBinHash, HEX $unsignedHexHash."
    }

    $targetMatches = [regex]::Matches($buildResult.Text, '\[(?<completed>\d+)\/(?<total>\d+)\]')
    $targetCount = $null
    if ($targetMatches.Count -gt 0) {
        $lastTarget = $targetMatches[$targetMatches.Count - 1]
        $targetCount = "$($lastTarget.Groups['completed'].Value)/$($lastTarget.Groups['total'].Value)"
    }
    $warningCount = [regex]::Matches($buildResult.Text, '(?im)^.*\bwarning:').Count

    $artifactSizes = [ordered]@{}
    foreach ($name in @($artifactPaths.Keys)) {
        if (Test-Path -LiteralPath $artifactPaths[$name] -PathType Leaf) {
            $artifactPaths[$name] = [IO.Path]::GetFullPath($artifactPaths[$name])
            $artifactSizes[$name] = (Get-Item -LiteralPath $artifactPaths[$name]).Length
        }
        else {
            $artifactPaths[$name] = $null
            $artifactSizes[$name] = $null
        }
    }

    $result = [ordered]@{
        status             = 'PASS'
        repository_root    = $repositoryRoot
        application_source = $applicationSource
        board              = $toolchain.validation.board
        build_type         = $toolchain.validation.build_type
        zephyr_commit      = $zephyrCommit
        mcxa_lib_commit     = $mcxaLibCommit
        build_version      = $buildVersion
        tool_versions      = [ordered]@{
            python     = $toolchain.python.version
            cmake      = $toolchain.cmake.version
            ninja      = $toolchain.ninja.version
            dtc        = $toolchain.dtc.version
            zephyr_sdk = $sdkVersion
            gcc        = $toolchain.zephyr_sdk.target_toolchain.gcc_version
        }
        artifacts          = $artifactPaths
        artifact_sizes     = $artifactSizes
        unsigned_sha256    = [ordered]@{
            bin = $unsignedBinHash
            hex = $unsignedHexHash
        }
        flash_used_bytes     = $memory.FlashUsedBytes
        flash_capacity_bytes = $memory.FlashCapacityBytes
        ram_used_bytes       = $memory.RamUsedBytes
        ram_capacity_bytes   = $memory.RamCapacityBytes
        warning_count        = $warningCount
        targets_completed    = $targetCount
        baseline_required    = [bool]$RequireBaseline
        baseline_match       = $baselineMatch
    }
    $resultPath = Join-Path $buildDirectory 'build-result.json'
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $resultPath -Encoding utf8

    Write-Stage 'Firmware build completed successfully'
    Write-Host "Targets completed : $targetCount"
    Write-Host "FLASH             : $($memory.FlashUsedBytes) / $($memory.FlashCapacityBytes) bytes"
    Write-Host "RAM               : $($memory.RamUsedBytes) / $($memory.RamCapacityBytes) bytes"
    Write-Host "Compiler warnings : $warningCount"
    Write-Host "BUILD_VERSION     : $buildVersion"
    Write-Host "Unsigned BIN SHA  : $unsignedBinHash"
    Write-Host "Unsigned HEX SHA  : $unsignedHexHash"
    foreach ($name in $artifactPaths.Keys) {
        if ($null -ne $artifactPaths[$name]) {
            Write-Host "$name : $($artifactPaths[$name]) ($($artifactSizes[$name]) bytes)"
        }
    }
    Write-Host "Build result      : $resultPath"
}

try {
    Invoke-FirmwareBuild
}
catch {
    Write-Error "BUILD FAILED: $($_.Exception.Message)"
    exit 1
}
