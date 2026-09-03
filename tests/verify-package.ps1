<#
.SYNOPSIS
    Installs the built ag WPM package, checks the machine state it creates, and
    removes it again.

.DESCRIPTION
    The install script is the part of the package a user cannot inspect before
    running, so this asserts each thing it promises:

      - ag.exe lands in %ProgramFiles%\Ag
      - AG_HOME is a machine-level variable naming that directory
      - the machine Path gains exactly one %AG_HOME% entry, as REG_EXPAND_SZ,
        with every pre-existing entry intact
      - installing twice does not add the entry twice
      - removal deletes the directory, the variable and the Path entry, and
        leaves the Path exactly as it was found
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('x86', 'x64', 'arm64')]
    [string]$Architecture,

    [ValidateSet('debug', 'release')]
    [string]$Flavor = 'release',

    [string]$PackageDirectory = 'out/packages'
)

$ErrorActionPreference = 'Stop'

# wpm reports failure through its exit code, which this script checks
# explicitly. PowerShell 7.4 would otherwise turn any non-zero exit into a
# terminating error before the check runs, losing the specific message.
$PSNativeCommandUseErrorActionPreference = $false

$environmentKey = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'

function Get-RawPath {
    # The stored Path must be read unexpanded, or the %AG_HOME% entry the
    # install script writes would come back already resolved.
    (Get-Item -LiteralPath $environmentKey).GetValue(
        'Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
}

function Get-MachineVariable {
    param([string]$Name)
    (Get-Item -LiteralPath $environmentKey).GetValue($Name, $null,
        [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
}

$pattern = if ($Flavor -eq 'debug') { "ag-$Architecture-debug-*.zip" } else { "ag-$Architecture-*.zip" }
$packages = @(Get-ChildItem -Path (Join-Path $PackageDirectory $pattern) -File)
if ($Flavor -eq 'release') {
    $packages = @($packages | Where-Object { $_.Name -notlike '*-debug-*' })
}
if ($packages.Count -ne 1) {
    throw "Expected exactly one $Flavor package for $Architecture, found $($packages.Count)."
}
$package = $packages[0]
$archiveName = [IO.Path]::GetFileNameWithoutExtension($package.Name)
Write-Host "Verifying $($package.Name)"

$installDirectory = if ($env:ProgramW6432) {
    Join-Path $env:ProgramW6432 'Ag'
} else {
    Join-Path $env:ProgramFiles 'Ag'
}

$originalPath = Get-RawPath
if ($originalPath -match [regex]::Escape('%AG_HOME%')) {
    throw 'The system Path already contains %AG_HOME% before installation.'
}

$installArguments = @('install', $package.FullName)
if ($Flavor -eq 'debug') { $installArguments += '--allow-unsigned' }

try {
    & wpm @installArguments
    if ($LASTEXITCODE -ne 0) { throw "wpm install failed with exit code $LASTEXITCODE." }

    $installed = Join-Path $installDirectory 'ag.exe'
    if (-not (Test-Path -LiteralPath $installed)) {
        throw "ag.exe was not installed to $installDirectory."
    }

    $agHome = Get-MachineVariable 'AG_HOME'
    if ($agHome -ne $installDirectory) {
        throw "AG_HOME is '$agHome' but the installation is at '$installDirectory'."
    }

    $updatedPath = Get-RawPath
    $entries = @($updatedPath -split ';' | Where-Object { $_ -eq '%AG_HOME%' })
    if ($entries.Count -ne 1) {
        throw "Expected one %AG_HOME% entry on the system Path, found $($entries.Count)."
    }
    $kind = (Get-Item -LiteralPath $environmentKey).GetValueKind('Path')
    if ($kind -ne [Microsoft.Win32.RegistryValueKind]::ExpandString) {
        throw "The system Path is stored as $kind; it must be REG_EXPAND_SZ to expand %AG_HOME%."
    }
    foreach ($entry in ($originalPath -split ';')) {
        if ($entry -and ($updatedPath -split ';') -notcontains $entry) {
            throw "Installation dropped the existing Path entry '$entry'."
        }
    }

    # The Path is only visible to new processes, so resolve ag through AG_HOME
    # the same way a new shell would.
    & $installed --version
    if ($LASTEXITCODE -ne 0) { throw 'The installed ag.exe did not run.' }

    # Installing again must be a no-op for the Path.
    & wpm @installArguments
    if ($LASTEXITCODE -ne 0) { throw "The second wpm install failed with exit code $LASTEXITCODE." }
    $entries = @((Get-RawPath) -split ';' | Where-Object { $_ -eq '%AG_HOME%' })
    if ($entries.Count -ne 1) {
        throw "Reinstalling produced $($entries.Count) %AG_HOME% Path entries."
    }
} finally {
    & wpm remove $archiveName
    $removeStatus = $LASTEXITCODE
}

if ($removeStatus -ne 0) { throw "wpm remove failed with exit code $removeStatus." }

if (Test-Path -LiteralPath $installDirectory) {
    throw "Removal left $installDirectory in place."
}
if ($null -ne (Get-MachineVariable 'AG_HOME')) {
    throw 'Removal left the AG_HOME variable behind.'
}
$finalPath = Get-RawPath
if ($finalPath -match [regex]::Escape('%AG_HOME%')) {
    throw 'Removal left the %AG_HOME% entry on the system Path.'
}
if ($finalPath -ne $originalPath) {
    throw "Removal did not restore the system Path.`nBefore: $originalPath`nAfter:  $finalPath"
}

Write-Host 'Package install, use and removal verified.'
