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
   pgrep -f '/Contents/MacOS/After Effects([[:space:]]|$)' \
       >/dev/null 2>&1; then
    echo "error: After Effects is running. Quit it before installing," >&2
    echo "       or AE will keep the old bundle loaded." >&2
    exit 1
fi

# Always build from scratch. An incremental directory can silently link
# stale objects compiled from since-reverted working-tree edits (ninja
# trusts mtimes, and git checkouts can leave sources older than objects),
# shipping code that no longer exists in the repo. That exact failure put a
# discarded rotation-origin experiment into an installed binary once; a
# clean build is cheap insurance against it happening again.
echo "==> Configuring ($build_dir, clean)"
rm -rf "$build_dir"
cmake -S "$repo_root" -B "$build_dir" -G Ninja

echo "==> Building"
cmake --build "$build_dir"

if [[ ! -d "$bundle" ]]; then
    echo "error: build finished but $bundle was not produced." >&2
    exit 1
fi

echo "==> Installing to $install_dir"
mkdir -p "$install_dir" 2>/dev/null || true
destination="$install_dir/SurfaceLab.plugin"
installed_without_sudo=false
# A system MediaCore directory can be root-owned while an existing development
# bundle is user-owned. In that common case, replace only the bundle contents;
# removing and recreating the top-level directory would unnecessarily require
# administrator access.
if [[ -d "$destination" && -w "$destination" ]]; then
    if rm -rf "$destination/Contents" 2>/dev/null && \
       cp -R "$bundle/Contents" "$destination/" 2>/dev/null; then
        installed_without_sudo=true
    fi
elif rm -rf "$destination" 2>/dev/null && \
     cp -R "$bundle" "$install_dir/" 2>/dev/null; then
    installed_without_sudo=true
fi
if [[ "$installed_without_sudo" != true ]]; then
    echo "    (retrying with sudo)"
    sudo mkdir -p "$install_dir"
    sudo rm -rf "$destination"
    sudo cp -R "$bundle" "$install_dir/"
fi

# AE refuses to load when the same match-name exists in both the system and
# user MediaCore folders. Keep only the chosen install target.
system_mc="/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
user_mc="${HOME}/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore"
for other in "$system_mc" "$user_mc"; do
    if [[ "$other" == "$install_dir" ]]; then
        continue
    fi
    if [[ -e "$other/SurfaceLab.plugin" ]]; then
        echo "==> Removing duplicate: $other/SurfaceLab.plugin"
        rm -rf "$other/SurfaceLab.plugin" 2>/dev/null || \
            sudo rm -rf "$other/SurfaceLab.plugin"
    fi
done

echo "==> Installed: $install_dir/SurfaceLab.plugin"
echo "    Start After Effects and apply Effect > SurfaceLab > SurfaceLab."
