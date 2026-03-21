param(
    [switch]$WhatIf
)

$paths = @(
    'C:\Users\conno\AppData\Local\UnrealEngine\Common\DerivedDataCache',
    'C:\Users\conno\AppData\Local\D3DSCache',
    'C:\Users\conno\Desktop\projects\golf-sim\ue\GolfSimUE\DerivedDataCache',
    'C:\Users\conno\Desktop\projects\golf-sim\ue\GolfSimUE\Intermediate',
    'C:\Users\conno\Desktop\projects\golf-sim\ue\GolfSimUE\Saved'
)

foreach ($path in $paths) {
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "Skip (not found): $path"
        continue
    }

    Write-Host "Clearing contents of: $path"

    if ($WhatIf) {
        Get-ChildItem -LiteralPath $path -Force | ForEach-Object {
            Write-Host "  Would remove: $($_.FullName)"
        }
    } else {
        Get-ChildItem -LiteralPath $path -Force |
            Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "Done."

