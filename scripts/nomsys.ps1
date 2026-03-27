## clear_msys_env.ps1 — Strip MSYS/Mingw env vars then run a command.
## Usage: pwsh scripts/clear_msys_env.ps1 cmake --preset esp32p4
##        pwsh scripts/clear_msys_env.ps1 cmake --build --preset esp32p4
Remove-Item Env:\MSYSTEM        -ErrorAction SilentlyContinue
Remove-Item Env:\OSTYPE         -ErrorAction SilentlyContinue
Remove-Item Env:\MSYS           -ErrorAction SilentlyContinue
Remove-Item Env:\MINGW_PREFIX   -ErrorAction SilentlyContinue
Remove-Item Env:\MINGW_CHOST    -ErrorAction SilentlyContinue
Remove-Item Env:\MSYSTEM_CHOST  -ErrorAction SilentlyContinue
Remove-Item Env:\MSYSTEM_PREFIX -ErrorAction SilentlyContinue

if ($args.Count -gt 0) {
    & $args[0] $args[1..($args.Count-1)]
    exit $LASTEXITCODE
}
