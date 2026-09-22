# Register arduino-cli into current user environment (PATH + ARDUINO_DIRECTORIES_*)
# Safe to re-run: skips if already set. Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\register_arduino_cli.ps1
param(
    [string]$CliDir = 'D:\dev\arduino-cli',
    [string]$DataDir = 'D:\dev\arduino_pack\data',
    [string]$UserDir = 'D:\dev\arduino_pack\user',
    [string]$DownloadsDir = 'D:\dev\arduino_pack\downloads'
)

$ErrorActionPreference = 'Stop'

# --- PATH: read raw from registry (do NOT expand %VAR%), append, keep value kind ---
$key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment', $true)
$kind = 'ExpandString'
try { $kind = $key.GetValueKind('Path') } catch { }   # create as ExpandString if missing
$raw = [string]$key.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)

if (($raw -split ';') -contains $CliDir) {
    Write-Output "PATH already contains $CliDir, skip"
} else {
    $new = if ([string]::IsNullOrWhiteSpace($raw)) { $CliDir } else { $raw.TrimEnd(';') + ';' + $CliDir }
    $key.SetValue('Path', $new, [Microsoft.Win32.RegistryValueKind]::$kind)
    Write-Output "PATH updated (kind=$kind): +$CliDir"
}

# --- ARDUINO_DIRECTORIES_*: plain string values ---
foreach ($pair in @(
    @{ Name = 'ARDUINO_DIRECTORIES_DATA';      Value = $DataDir },
    @{ Name = 'ARDUINO_DIRECTORIES_USER';      Value = $UserDir },
    @{ Name = 'ARDUINO_DIRECTORIES_DOWNLOADS'; Value = $DownloadsDir }
)) {
    $cur = [string]$key.GetValue($pair.Name, '')
    if ($cur -ieq $pair.Value) {
        Write-Output "$($pair.Name) already = $cur, skip"
    } else {
        if ($cur) { Write-Output "NOTE: $($pair.Name) '$cur' will be overwritten with '$($pair.Value)'" }
        $key.SetValue($pair.Name, $pair.Value, [Microsoft.Win32.RegistryValueKind]::String)
        Write-Output "$($pair.Name) = $($pair.Value)"
    }
}
$key.Close()

# --- Broadcast WM_SETTINGCHANGE so new terminals get the new env immediately ---
Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
'@
[UIntPtr]$result = [UIntPtr]::Zero
[void][Win32.NativeMethods]::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result)
Write-Output 'Environment change broadcast sent (new terminals will see it)'
