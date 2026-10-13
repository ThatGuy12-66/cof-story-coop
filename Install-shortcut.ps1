$ErrorActionPreference = 'Stop'
$launcherRoot = $PSScriptRoot
$pythonCommand = Get-Command python.exe -ErrorAction Stop
$pythonWindowless = Join-Path (Split-Path -Parent $pythonCommand.Source) 'pythonw.exe'
if (-not (Test-Path -LiteralPath $pythonWindowless)) {
    throw 'pythonw.exe was not found next to Python. Install Python with Tk support or run python launch.pyw.'
}
$desktopFolder = [Environment]::GetFolderPath('Desktop')
$shortcutPath = Join-Path $desktopFolder 'Cry of Fear Story Co-op.lnk'
$shellObject = New-Object -ComObject WScript.Shell
$shortcut = $shellObject.CreateShortcut($shortcutPath)
if ((Test-Path -LiteralPath $shortcutPath) -and $shortcut.Arguments -ne ('"' + (Join-Path $launcherRoot 'launch.pyw') + '"')) {
    throw "An unrelated shortcut already exists at $shortcutPath. Rename it before running this installer."
}
$shortcut.TargetPath = $pythonWindowless
$shortcut.Arguments = '"' + (Join-Path $launcherRoot 'launch.pyw') + '"'
$shortcut.WorkingDirectory = $launcherRoot
$shortcut.Description = 'Host or join the Cry of Fear story campaign using the local development copy'
$shortcut.IconLocation = (Join-Path $launcherRoot 'baseline\Cry of Fear\cof.exe') + ',0'
$shortcut.Save()
Write-Output $shortcutPath
