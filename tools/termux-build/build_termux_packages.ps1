$ErrorActionPreference = "Stop"

$root = Resolve-Path "${PSScriptRoot}\..\.."
$rootWsl = wsl wslpath -a $root

$cmd = "cd $rootWsl; ./tools/termux-build/build_termux_packages.sh"

wsl bash -lc $cmd
