#!/usr/bin/env bash
# Build, test, ad-hoc sign, and install ButterComp Studio for Apple Silicon.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
install_only=false
bundle=""
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    cat <<'HELP'
Usage:
  bash scripts/build-sign-install-mac.sh [extra CMake configure arguments]
  bash scripts/build-sign-install-mac.sh --install-only "/path/to/ButterComp Studio.vst3"

Builds and tests an arm64 VST3, ad-hoc signs it, then installs to:
  ~/Library/Audio/Plug-Ins/VST3/ButterComp Studio.vst3

Requires macOS on Apple Silicon (M2 and later supported), Xcode Command Line
Tools, and CMake 3.22+. Install-only mode needs only the downloaded VST3 bundle.
Optional: BUTTERCOMP_BUILD_DIR and BUTTERCOMP_INSTALL_DIR override the directories.
Close your DAW before installing; restart it or rescan plugins afterwards.
HELP
    exit 0
fi
if [[ "${1:-}" == "--install-only" ]]; then
    [[ $# -eq 2 ]] || { echo "Usage: $0 --install-only '/path/to/ButterComp Studio.vst3'" >&2; exit 2; }
    install_only=true
    bundle="$2"
fi
[[ "$(uname -s)" == "Darwin" ]] || { echo "This script requires macOS." >&2; exit 1; }
[[ "$(uname -m)" == "arm64" ]] || { echo "Run from a native Apple Silicon terminal (disable Rosetta)." >&2; exit 1; }
for tool in codesign ditto; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
if [[ "$install_only" == false ]]; then
    for tool in cmake ctest; do
        command -v "$tool" >/dev/null || { echo "Install CMake 3.22+ first (brew install cmake)." >&2; exit 1; }
    done
    build_dir="${BUTTERCOMP_BUILD_DIR:-$project_dir/build-mac}"
    cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
        -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 "$@"
    cmake --build "$build_dir" --config Release --parallel 3
    ctest --test-dir "$build_dir" -C Release --output-on-failure
    bundle="$build_dir/ButterCompStudio_artefacts/Release/VST3/ButterComp Studio.vst3"
fi
[[ -d "$bundle/Contents" ]] || { echo "VST3 bundle not found: $bundle" >&2; exit 1; }
[[ -f "$bundle/Contents/MacOS/ButterComp Studio" ]] || { echo "Not a ButterComp Studio macOS VST3 bundle: $bundle" >&2; exit 1; }
install_dir="${BUTTERCOMP_INSTALL_DIR:-$HOME/Library/Audio/Plug-Ins/VST3}"
mkdir -p "$install_dir"
staging="$(mktemp -d "$install_dir/.ButterComp Studio-install.XXXXXX")"
destination="$install_dir/ButterComp Studio.vst3"
cleanup() {
    if [[ -d "$staging/previous.vst3" ]]; then
        if [[ ! -e "$destination" && ! -L "$destination" ]]; then
            if ! mv "$staging/previous.vst3" "$destination"; then
                echo "Could not restore the previous installation. Backup retained at: $staging" >&2
                return
            fi
        else
            # After a successful install, the new destination replaces the backup.
            if [[ "$installed" != true ]]; then
                echo "Previous installation retained at: $staging/previous.vst3" >&2
                return
            fi
        fi
    fi
    rm -rf "$staging"
}
installed=false
trap cleanup EXIT
# Sign the staged copy, preserving the downloaded/build bundle and any existing
# installed version until signing and verification succeed.
ditto "$bundle" "$staging/ButterComp Studio.vst3"
codesign --force --deep --sign - "$staging/ButterComp Studio.vst3"
codesign --verify --deep --strict "$staging/ButterComp Studio.vst3"
if [[ -e "$destination" ]]; then mv "$destination" "$staging/previous.vst3"; fi
mv "$staging/ButterComp Studio.vst3" "$destination"
installed=true
printf 'Installed ButterComp Studio: %s\nRestart or rescan plugins in your VST3 DAW.\n' "$destination"
