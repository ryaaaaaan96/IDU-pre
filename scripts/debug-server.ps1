# Run in Windows PowerShell. The J-Link USB probe stays on Windows.
[CmdletBinding()]
param(
    [string]$Device,
    [string]$JLinkDir,
    [ValidateRange(1, 65535)][int]$Port = 2331,
    [ValidateRange(1, 50000)][int]$Speed = 1000,
    [string]$SerialNumber,
    [switch]$LocalhostOnly,
    [switch]$DryRun,
    [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($Help) {
    Write-Output @'
Usage: .\scripts\debug-server.ps1 -Device <confirmed J-Link device name> [options]
  -JLinkDir DIR     J-Link install directory; auto-detected if omitted
  -Port PORT        GDB TCP port (default: 2331)
  -Speed KHZ        SWD clock (default: 1000 kHz)
  -SerialNumber SN  Select a specific USB J-Link
  -LocalhostOnly    Listen on localhost only (for mirrored WSL)
  -DryRun           Print the command without accessing the probe
  -Help             Show help

Default: allow remote clients (NAT WSL); no firewall rules are changed.
The server loads the Zephyr RTOS plugin and halts without reset or flashing.
Device names are not substituted: confirm the GD32E505VET7 mapping in J-Link.
See docs/wsl-jlink.md for network setup and external watchdog handling.
'@
    exit 0
}

if ([string]::IsNullOrWhiteSpace($Device)) {
    throw 'Specify -Device using the name confirmed in the J-Link device selector. Use -Help for details.'
}

if ([string]::IsNullOrWhiteSpace($JLinkDir)) {
    $installedCommand = Get-Command 'JLinkGDBServerCL.exe' -CommandType Application -ErrorAction SilentlyContinue
    if ($installedCommand) {
        $JLinkDir = Split-Path -Parent $installedCommand.Source
    }
    if (-not $JLinkDir) {
        $registry = Get-ItemProperty 'HKCU:\Software\SEGGER\J-Link' -ErrorAction SilentlyContinue
        if ($registry -and $registry.PSObject.Properties['InstallPath']) {
            $JLinkDir = $registry.InstallPath
        }
    }
    if (-not $JLinkDir -and $env:ProgramFiles) {
        $installRoot = Join-Path $env:ProgramFiles 'SEGGER'
        $install = Get-ChildItem -LiteralPath $installRoot -Directory -Filter 'JLink*' -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'JLinkGDBServerCL.exe') -PathType Leaf } |
            Sort-Object -Property @{ Expression = { [long]($_.Name -replace '\D', '0') }; Descending = $true }, Name |
            Select-Object -First 1
        if ($install) { $JLinkDir = $install.FullName }
    }
}
if (-not $JLinkDir) { throw 'J-Link installation not found. Supply -JLinkDir.' }

$serverPath = Join-Path $JLinkDir 'JLinkGDBServerCL.exe'
$pluginPath = Join-Path $JLinkDir 'GDBServer\RTOSPlugin_Zephyr.dll'
foreach ($requiredFile in @($serverPath, $pluginPath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file not found: $requiredFile"
    }
}

$listenState = if ($LocalhostOnly) { '1' } else { '0' }
$serverArguments = @(
    '-device', $Device, '-if', 'SWD', '-endian', 'little',
    '-speed', "$Speed", '-port', "$Port", '-LocalhostOnly', $listenState,
    '-noir', '-noreset', '-halt', '-rtos', $pluginPath
)
if ($SerialNumber) { $serverArguments += @('-USB', $SerialNumber) }

Write-Host "Zephyr GDB Server: TCP $Port, device $Device, SWD $Speed kHz"
if ($DryRun) {
    $quotedCommand = (@($serverPath) + $serverArguments) | ForEach-Object { "'" + $_.Replace("'", "''") + "'" }
    Write-Output ('& ' + ($quotedCommand -join ' '))
    exit 0
}
& $serverPath @serverArguments
exit $LASTEXITCODE
