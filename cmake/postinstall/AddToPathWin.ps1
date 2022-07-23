#
# src: https://stackoverflow.com/a/69239861/ with minor modifications:
# Removed unnecessary check for scope; always use CurrentUser instead
# Removed update of script process's path, since it terminates immediately anyway
# Made printing consistent with that of `ninja install`
#
param(
  [Parameter(Mandatory, Position=0)]
  [string] $LiteralPath
)

Set-StrictMode -Version 1; $ErrorActionPreference = 'Stop'

$regPath = 'registry::HKEY_CURRENT_USER\Environment'

# Note the use of the .GetValue() method to unsure that the *unexpanded* value is returned.
$currDirs = (Get-Item -LiteralPath $regPath).GetValue('Path', '', 'DoNotExpandEnvironmentNames') -split ';' -ne ''

# Check if supplied path is already in user-level path; if so, exit.
if ($LiteralPath -in $currDirs) {
  Write-Output "-- Up-to-date: $LiteralPath`\`r`n               is already present in the persistent user-level path.`r`n"
  return
}
# Otherwise, add it to the string of paths,
$newValue = ($currDirs + $LiteralPath) -join ';'

# and update the registry with that string. 
Set-ItemProperty -Type ExpandString -LiteralPath $regPath Path $newValue

# Broadcast a WM_SETTINGCHANGE to get the Windows shell to reload the
# updated environment, via a dummy [Environment]::SetEnvironmentVariable() operation.
$dummyName = [guid]::NewGuid().ToString()
[Environment]::SetEnvironmentVariable($dummyName, 'foo', 'User')
[Environment]::SetEnvironmentVariable($dummyName, [NullString]::value, 'User')

Write-Output "-- Installing: $LiteralPath`\`r`n               was successfully appended to the persistent user-level path."

