Termux Package Build (WSL2)

This builds a large arm64 toolset using termux-packages and bundles it into
app/src/main/assets/termux-root.zip. The app will extract it to
/data/data/<package>/files/usr on first run.

Prerequisites (WSL2 Ubuntu):
- sudo apt update
- sudo apt install -y git build-essential python3 zip unzip dpkg-dev

Run from Windows PowerShell:
- .\tools\termux-build\build_termux_packages.ps1

Notes:
- Build time and disk usage are large.
- Package list: tools/termux-build/package-list.txt
- If a package fails, remove it from the list and rerun.
