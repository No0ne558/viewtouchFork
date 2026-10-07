#!/bin/sh
# Install ViewTouch on a new Linux machine, then set up what it is.
#
#   sudo sh install-viewtouch.sh                      build from the source, install, then ask
#   sudo sh install-viewtouch.sh vtmodern-*.rpm       install this package (or a .deb), then ask
#   sudo sh install-viewtouch.sh [PACKAGE] -- kitchen auto Grill
#                                                     ...and set it up without asking
#                                                     (anything after -- goes to vtmodern-setup)
#
# Or, on a machine with nothing on it yet:
#   curl -fsSL https://raw.githubusercontent.com/No0ne558/viewtouchFork/Modernization/modern/packaging/install-viewtouch.sh | sudo sh
#
# Works on Fedora (and RHEL-likes) and Debian, Ubuntu and Raspberry Pi OS.
# Without a package it fetches the source (git), installs the build tools,
# builds a package for this machine and installs it: a few minutes.
set -eu

REPO=${VTM_REPO:-https://github.com/No0ne558/viewtouchFork.git}
BRANCH=${VTM_BRANCH:-Modernization}
SRC=${VTM_SRC:-/opt/viewtouch-src}

say() { printf '\n== %s\n' "$*"; }
die() { echo "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] || die "Run this with sudo."
[ -r /etc/os-release ] || die "Can't tell which Linux this is (no /etc/os-release)."
. /etc/os-release
case " ${ID:-} ${ID_LIKE:-} " in
    *" fedora "*|*" rhel "*) family=fedora ;;
    *" debian "*|*" ubuntu "*) family=debian ;;
    *) die "This installer knows Fedora, Debian, Ubuntu and Raspberry Pi OS; this is ${PRETTY_NAME:-unknown}." ;;
esac

pkg_install() {
    if [ "$family" = fedora ]; then
        dnf -y install "$@"
    else
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
    fi
}

# --- the arguments: a package file, and what follows -- for vtmodern-setup -----------
package=
while [ $# -gt 0 ]; do
    case "$1" in
        --) shift; break ;;
        -h|--help) sed -n '2,16p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *.rpm|*.deb) package=$1; shift ;;
        *) die "Unknown argument: $1 (a .rpm or .deb package, then -- and the setup)" ;;
    esac
done

# --- get a package ----------------------------------------------------------------
if [ -z "$package" ]; then
    here=$(cd "$(dirname "$0")" 2>/dev/null && pwd || true)
    if [ -n "$here" ] && [ -x "$here/build-packages.sh" ]; then
        modern=$(dirname "$here")   # run from a copy of the source
    else
        say "Getting the source"
        command -v git >/dev/null 2>&1 || pkg_install git
        if [ -d "$SRC/.git" ]; then
            git -C "$SRC" fetch --depth 1 origin "$BRANCH" && git -C "$SRC" checkout -q FETCH_HEAD
        else
            git clone --depth 1 --branch "$BRANCH" "$REPO" "$SRC"
        fi
        modern=$SRC/modern
    fi
    say "Installing the build tools"
    sh "$modern/packaging/build-packages.sh" deps
    say "Building ViewTouch for this machine (a few minutes)"
    sh "$modern/packaging/build-packages.sh"
    if [ "$family" = fedora ]; then
        package=$(ls -t "$modern"/dist/vtmodern-*.rpm 2>/dev/null | head -1)
    else
        package=$(ls -t "$modern"/dist/vtmodern_*.deb 2>/dev/null | head -1)
    fi
    [ -n "$package" ] || die "The build made no package; see the messages above."
fi
[ -f "$package" ] || die "No such package: $package"

# --- install it, and the screen's kiosk ------------------------------------------------
say "Installing $(basename "$package")"
case "$package" in
    /*) ;;
    *) package=$(pwd)/$package ;;
esac
pkg_install "$package"
# cage runs ViewTouch full screen on a touch screen (not needed for a server
# without a screen, but small).
command -v cage >/dev/null 2>&1 || pkg_install cage || echo "Couldn't install cage: screens need it (install it later)."

# --- what is this machine? -------------------------------------------------------------
say "Setting up this machine"
if [ $# -gt 0 ]; then
    vtmodern-setup "$@"
elif [ -r /dev/tty ]; then
    vtmodern-setup < /dev/tty   # the menu, even when this script came through a pipe
else
    echo "Installed. Now run:  sudo vtmodern-setup   (it asks what this machine is)"
fi
