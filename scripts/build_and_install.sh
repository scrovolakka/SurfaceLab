#!/bin/bash
# Build SurfaceLab.plugin and install it into an After Effects plug-ins folder.
#
# macOS only (needs Xcode's clang/Rez/codesign and the AE SDK at
# work/vendor/AfterEffectsSDK/ae25.6_61.64bit.AfterEffectsSDK — see README).
#
# Usage:
#   scripts/build_and_install.sh                 # build + install to MediaCore
#   SURFACELAB_INSTALL_DIR=/path scripts/build_and_install.sh
#
# The default destination is the shared MediaCore folder, which every AE
# version loads. Writing there may require sudo; the script retries with sudo
# only for the copy step.

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="$repo_root/work/build/surfacelab"
bundle="$build_dir/SurfaceLab.plugin"
install_dir="${SURFACELAB_INSTALL_DIR:-/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore}"

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: the plug-in build requires macOS (Xcode + AE SDK)." >&2
    exit 1
fi

if pgrep -x "After Effects" >/dev/null 2>&1 || \
   pgrep -f "Adobe After Effects" >/dev/null 2>&1; then
    echo "error: After Effects is running. Quit it before installing," >&2
    echo "       or AE will keep the old bundle loaded." >&2
    exit 1
fi

echo "==> Configuring ($build_dir)"
cmake -S "$repo_root" -B "$build_dir" -G Ninja

echo "==> Building"
cmake --build "$build_dir"

if [[ ! -d "$bundle" ]]; then
    echo "error: build finished but $bundle was not produced." >&2
    exit 1
fi

echo "==> Installing to $install_dir"
mkdir -p "$install_dir" 2>/dev/null || true
if ! rm -rf "$install_dir/SurfaceLab.plugin" 2>/dev/null || \
   ! cp -R "$bundle" "$install_dir/" 2>/dev/null; then
    echo "    (retrying with sudo)"
    sudo mkdir -p "$install_dir"
    sudo rm -rf "$install_dir/SurfaceLab.plugin"
    sudo cp -R "$bundle" "$install_dir/"
fi

echo "==> Installed: $install_dir/SurfaceLab.plugin"
echo "    Start After Effects and apply Effect > SurfaceLab > SurfaceLab."
