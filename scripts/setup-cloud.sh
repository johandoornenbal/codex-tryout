#!/usr/bin/env bash
set -euo pipefail
cd /workspace/codex-tryout
python3 -m pip install --user cmake==4.4.4 ninja==1.13.2
if [ ! -d /workspace/deps/JUCE/.git ]; then
  git clone --branch 8.0.12 --depth 1 https://github.com/juce-framework/JUCE.git /workspace/deps/JUCE
fi
test "$(git -C /workspace/deps/JUCE rev-parse HEAD)" = 29396c22c93392d6738e021b83196283d6e4d850 || {
  echo 'JUCE must be at pinned commit 29396c22c93392d6738e021b83196283d6e4d850. Preserve any local changes before selecting it.' >&2
  exit 1
}
# Dependencies are installed outside the project in a rootless sysroot. Refresh
# signed metadata and verify package checksums using apt; never disable trust.
mkdir -p /tmp/butter-apt/lists/partial /tmp/butter-apt/cache/archives/partial /tmp/plugin-debs /workspace/sysroot
printf 'deb https://deb.debian.org/debian trixie main\n' > /tmp/butter-apt/sources.list
apt_options=(-o Dir::Etc::sourcelist=/tmp/butter-apt/sources.list -o Dir::Etc::sourceparts=- -o Dir::Etc::parts=- -o Dir::State::lists=/tmp/butter-apt/lists -o Dir::Cache=/tmp/butter-apt/cache)
/usr/bin/apt-get "${apt_options[@]}" update
(
 cd /tmp/plugin-debs
 /usr/bin/apt-get "${apt_options[@]}" download libasound2-dev libfreetype-dev libfontconfig-dev libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxrender-dev libxfixes-dev libgl-dev libglx-dev x11proto-dev libpng-dev libbrotli-dev libbz2-dev zlib1g-dev libharfbuzz-dev libexpat1-dev/trixie libpthread-stubs0-dev libxau-dev libxdmcp-dev libxcb1-dev xtrans-dev uuid-dev libgraphite2-dev
 for package in ./*.deb; do dpkg-deb -x "$package" /workspace/sysroot; done
)
# Development packages have linker symlinks; use the instance's matching runtime
# libraries where installed instead of accidentally selecting static archives.
python3 - <<'PY'
from pathlib import Path
import os
root=Path('/workspace/sysroot/usr/lib/x86_64-linux-gnu')
for link in root.glob('*.so'):
    if link.is_symlink() and not link.exists():
        name=Path(os.readlink(link)).name
        runtime=Path('/usr/lib/x86_64-linux-gnu')/name
        if runtime.exists(): (root/name).symlink_to(runtime)
PY
source scripts/cloud-env.sh
cmake -S . -B build -DJUCE_PATH=/workspace/deps/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
