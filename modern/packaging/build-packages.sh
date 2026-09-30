#!/bin/sh
# Build ViewTouch packages into modern/dist/.
#
#   packaging/build-packages.sh              for this machine (.rpm on Fedora, .deb on Debian/Ubuntu)
#   packaging/build-packages.sh fedora       in a Fedora container (podman or docker)
#   packaging/build-packages.sh debian       in a Debian 13 container (also for Raspberry Pi OS)
#   packaging/build-packages.sh ubuntu       in an Ubuntu 26.04 container
#   packaging/build-packages.sh deps         install the build tools here (as root)
#
# Containers build for this machine's CPU (x86_64 or aarch64). For the other
# one, run it there, or use the "Modern packages" GitHub workflow.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
modern=$(dirname "$here")
repo=$(dirname "$modern")
dist=$modern/dist

build_here() {
    . /etc/os-release
    case " ${ID:-} ${ID_LIKE:-} " in
        *" fedora "*|*" rhel "*) generator=RPM ;;
        *" debian "*|*" ubuntu "*) generator=DEB ;;
        *) echo "Unknown distribution ${ID:-}; building both" >&2; generator="DEB;RPM" ;;
    esac
    build=${BUILD_DIR:-$modern/build-package-${ID:-linux}-$(uname -m)}
    cmake -S "$modern" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
          -DVTM_BUILD_TESTS=OFF
    cmake --build "$build"
    mkdir -p "$dist"
    (cd "$build" && cpack -G "$generator" -B "$dist/.cpack-$$")
    find "$dist/.cpack-$$" -maxdepth 1 \( -name '*.deb' -o -name '*.rpm' \) -exec mv {} "$dist" \;
    rm -rf "$dist/.cpack-$$"
    ls -l "$dist"
}

deps_fedora="gcc-c++ cmake ninja-build rpm-build qt6-qtbase-devel qt6-qtdeclarative-devel"
deps_debian="g++ cmake ninja-build dpkg-dev file qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools libgl-dev"

install_deps() {
    . /etc/os-release
    case " ${ID:-} ${ID_LIKE:-} " in
        *" fedora "*|*" rhel "*) dnf -y install $deps_fedora ;;
        *" debian "*|*" ubuntu "*)
            apt-get update
            DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $deps_debian ;;
        *) echo "Install a C++23 compiler, CMake, Ninja and Qt 6.8+ (base, declarative) yourself." >&2; exit 1 ;;
    esac
}

in_container() {
    image=$1
    install=$2
    engine=$(command -v podman || command -v docker || true)
    if [ -z "$engine" ]; then
        echo "Install podman (or docker) to build in a container." >&2
        exit 1
    fi
    # The whole repository is mounted: the build uses textures from the legacy tree.
    "$engine" run --rm -v "$repo:/src:Z" -e BUILD_DIR=/tmp/build "$image" \
        sh -c "$install && /src/modern/packaging/build-packages.sh"
}

case "${1:-}" in
"") build_here ;;
deps) install_deps ;;
fedora) in_container fedora:44 "/src/modern/packaging/build-packages.sh deps" ;;
debian) in_container debian:trixie "/src/modern/packaging/build-packages.sh deps" ;;
ubuntu) in_container ubuntu:26.04 "/src/modern/packaging/build-packages.sh deps" ;;
-h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//' ;;
*) echo "Unknown target: $1" >&2; exit 1 ;;
esac
