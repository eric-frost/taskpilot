#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Eric Frost
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build TaskPilot's release packages inside Podman containers, one per target
# distro, so each package links against and depends on that distro's own Qt
# and KDE Frameworks.  Then install every package into a fresh container of its
# distro and run it, to prove its dependencies resolve.
#
# Packages are built from the committed HEAD (git archive), never from
# uncommitted changes.
#
# Usage: packaging/build-packages.sh [OUTDIR] [TARGET...]
#   OUTDIR defaults to ./dist; TARGETs default to all of:
#   ubuntu-26.04 debian-13 fedora-44 opensuse-tumbleweed arch
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=$(realpath -m "${1:-$root/dist}")
shift || true
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(ubuntu-26.04 debian-13 fedora-44 opensuse-tumbleweed arch)
version=$(sed -n 's/^project(taskpilot VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
src="taskpilot-$version"

# Build dependencies shared by the Debian-family and RPM-family targets.
deb_deps=(cmake extra-cmake-modules g++ dpkg-dev file qt6-base-dev libkf6coreaddons-dev
  libkf6i18n-dev libkf6xmlgui-dev libkf6config-dev libkf6configwidgets-dev libkf6crash-dev
  libkf6auth-dev libkf6itemmodels-dev libkf6itemviews-dev libkf6widgetsaddons-dev)
rpm_kf6=(kf6-kauth-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-kxmlgui-devel
  kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-kcrash-devel kf6-kitemmodels-devel
  kf6-kitemviews-devel kf6-kwidgetsaddons-devel)

# CMake build + CPack inside the container; $1 is the CPack generator.
cpack_build() {
    echo "tar xf /work/$src.tar.gz -C /tmp
          cmake -S /tmp/$src -B /tmp/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
          cmake --build /tmp/build -j\$(nproc)
          cd /tmp/build && cpack -G $1"
}

# Per target: image, build script, the glob CPack/makepkg writes, the name to
# publish it under, and the command that installs it in a fresh container.
target_info() {
    case "$1" in
    ubuntu-26.04|debian-13)
        image=docker.io/library/${1/-/:}
        build="export DEBIAN_FRONTEND=noninteractive
               apt-get update -qq && apt-get install -y -qq --no-install-recommends ${deb_deps[*]} >/dev/null
               $(cpack_build DEB) && cp /tmp/build/*.deb /work/"
        built="taskpilot_${version}_amd64.deb"
        name="taskpilot_${version}_$1_amd64.deb"
        install="export DEBIAN_FRONTEND=noninteractive
                 apt-get update -qq && apt-get install -y -qq /work/$name >/dev/null" ;;
    fedora-44)
        image=registry.fedoraproject.org/fedora:44
        build="dnf install -y -q cmake gcc-c++ rpm-build extra-cmake-modules qt6-qtbase-devel ${rpm_kf6[*]}
               $(cpack_build RPM) && cp /tmp/build/*.rpm /work/"
        built="taskpilot-$version-1.fc44.x86_64.rpm"
        name="$built"
        install="dnf install -y -q /work/$name" ;;
    opensuse-tumbleweed)
        image=registry.opensuse.org/opensuse/tumbleweed:latest
        build="zypper -q -n install cmake gcc-c++ rpm-build kf6-extra-cmake-modules qt6-base-devel ${rpm_kf6[*]}
               $(cpack_build RPM) && cp /tmp/build/*.rpm /work/"
        built="taskpilot-$version-1.x86_64.rpm"
        name="taskpilot-$version-1.opensuse-tumbleweed.x86_64.rpm"
        install="zypper -q -n --no-gpg-checks install --allow-unsigned-rpm /work/$name" ;;
    arch)
        image=docker.io/library/archlinux:latest
        build="pacman -Syu --noconfirm --needed -q base-devel cmake extra-cmake-modules qt6-base \
                 kauth kcoreaddons ki18n kxmlgui kconfig kconfigwidgets kcrash kitemmodels \
                 kitemviews kwidgetsaddons polkit >/dev/null
               useradd -m builder
               install -d -o builder /tmp/pkg
               cp /work/PKGBUILD /work/$src.tar.gz /tmp/pkg/ && chown builder /tmp/pkg/*
               su builder -c 'cd /tmp/pkg && makepkg --nodeps --noconfirm'
               cp /tmp/pkg/*.pkg.tar.zst /work/"
        built="taskpilot-$version-1-x86_64.pkg.tar.zst"
        name="$built"
        install="pacman -Sy --noconfirm -q >/dev/null && pacman -U --noconfirm /work/$name >/dev/null" ;;
    *)
        echo "unknown target: $1" >&2; exit 1 ;;
    esac
}

# 1. Source tarball from the committed tree, laid out like GitHub's tag archive.
work=$(mktemp -d "${TMPDIR:-/tmp}/taskpilot-pkg.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir -p "$out"
git -C "$root" archive --format=tar.gz --prefix="$src/" -o "$out/$src.tar.gz" HEAD
cp "$out/$src.tar.gz" "$root/packaging/arch/PKGBUILD" "$work/"

for t in "${targets[@]}"; do
    target_info "$t"

    # 2. Build the package in a container of the target distro.
    echo "== $t: building in $image"
    podman run --rm -v "$work:/work" "$image" bash -euc "$build" >"$out/build-$t.log" 2>&1 \
        || { echo "build failed, see $out/build-$t.log" >&2; exit 1; }
    [ "$built" = "$name" ] || mv "$work/$built" "$work/$name"

    # 3. Install it into a fresh container and run it headless.
    echo "== $t: test install"
    podman run --rm -v "$work:/work" "$image" bash -euc "$install
        QT_QPA_PLATFORM=offscreen taskpilot --version
        test -f /usr/share/polkit-1/actions/io.github.ericfrost.taskpilot.policy
        find /usr -name taskpilot_auth -type f | grep -q ." >>"$out/build-$t.log" 2>&1 \
        || { echo "test install failed, see $out/build-$t.log" >&2; exit 1; }
    tail -n 1 "$out/build-$t.log"
    cp "$work/$name" "$out/"
done

# 4. Checksums for everything published.
(cd "$out" && sha256sum taskpilot[-_]* > SHA256SUMS)
echo "packages in $out"
