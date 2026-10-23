#!/usr/bin/env bash
# build.sh — Build PicoW-Rescue-NIC firmware, with WiFi settings as arguments.
#
# The point: change the SSID / password / channel WITHOUT editing any file.
# (It still compiles — but it's one command instead of "edit CMakeLists,
#  cmake, make".)
#
# Usage:
#   ./scripts/build.sh                                  # defaults (CHANGE THE PASSWORD!)
#   ./scripts/build.sh --ssid MyRescue --pass 's3cret-passphrase'
#   ./scripts/build.sh --ssid MyRescue --pass '...' --channel 11
#   ./scripts/build.sh --clean                          # wipe build dir first
#
# Options:
#   --ssid     <name>    WiFi SSID              (default: PicoBridge)
#   --pass     <phrase>  WPA2 passphrase, 8-63  (default: picobridge123 — CHANGE IT)
#   --channel  <1-13>    2.4GHz channel         (default: 6)
#   --version  <vX.Y>    Firmware version string shown in the boot banner
#   --board    <board>   Pico board             (default: pico_w)
#   --build-dir <dir>    Build directory        (default: build)
#   --clean              Remove the build dir before configuring
#   --fetch-picotool     Make the SDK download & build the exact picotool it
#                        needs (PICOTOOL_FORCE_FETCH_FROM_GIT=ON). Use this if
#                        configure fails with "Incompatible picotool ... version
#                        unknown" because a mismatched picotool is installed.
#   -h, --help           Show this help
#
# Requirements:
#   - PICO_SDK_PATH set (or a pico-sdk checkout next to this repo)
#   - cmake, make, arm-none-eabi-gcc (see README "前提条件" / "Prerequisites")
#
# Output: <build-dir>/picow_nic.uf2   → flash with ./scripts/flash.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# ── Colors ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[build]${NC} $*"; }
warn()  { echo -e "${YELLOW}[build]${NC} $*"; }
error() { echo -e "${RED}[build] ERROR:${NC} $*" >&2; }

usage() { sed -n '2,32p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

# ── Defaults (empty = let CMakeLists.txt supply its own default) ─────────────
SSID=""; PASS=""; CHANNEL=""; FW_VERSION=""
BOARD="pico_w"
BUILD_DIR="${REPO_ROOT}/build"
CLEAN=0
FETCH_PICOTOOL=0

# ── Parse args ───────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ssid)      SSID="${2:?--ssid needs a value}"; shift 2 ;;
        --pass|--password) PASS="${2:?--pass needs a value}"; shift 2 ;;
        --channel)   CHANNEL="${2:?--channel needs a value}"; shift 2 ;;
        --version)   FW_VERSION="${2:?--version needs a value}"; shift 2 ;;
        --board)     BOARD="${2:?--board needs a value}"; shift 2 ;;
        --build-dir) BUILD_DIR="${2:?--build-dir needs a value}"; shift 2 ;;
        --clean)     CLEAN=1; shift ;;
        --fetch-picotool) FETCH_PICOTOOL=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *) error "Unknown option: $1"; echo; usage; exit 1 ;;
    esac
done

# ── Validate (only what the user actually passed) ────────────────────────────
if [[ -n "${PASS}" ]]; then
    len=${#PASS}
    if (( len < 8 || len > 63 )); then
        error "WPA2 passphrase must be 8-63 characters (got ${len})."
        exit 1
    fi
fi
if [[ -n "${SSID}" ]] && (( ${#SSID} > 32 )); then
    error "SSID must be 1-32 characters (got ${#SSID})."
    exit 1
fi
if [[ -n "${CHANNEL}" ]]; then
    if ! [[ "${CHANNEL}" =~ ^[0-9]+$ ]] || (( CHANNEL < 1 || CHANNEL > 13 )); then
        error "WiFi channel must be an integer 1-13 (got '${CHANNEL}')."
        exit 1
    fi
fi

# ── Locate the Pico SDK ──────────────────────────────────────────────────────
if [[ -z "${PICO_SDK_PATH:-}" ]]; then
    if [[ -d "${REPO_ROOT}/../pico-sdk" ]]; then
        export PICO_SDK_PATH="$(cd "${REPO_ROOT}/../pico-sdk" && pwd)"
        info "PICO_SDK_PATH not set — using ${PICO_SDK_PATH}"
    else
        error "PICO_SDK_PATH is not set and ../pico-sdk was not found."
        echo  "  git clone https://github.com/raspberrypi/pico-sdk --recurse-submodules"
        echo  "  export PICO_SDK_PATH=/path/to/pico-sdk"
        exit 1
    fi
fi

# ── Configure ────────────────────────────────────────────────────────────────
if (( CLEAN )) && [[ -d "${BUILD_DIR}" ]]; then
    info "Cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"

CMAKE_ARGS=(
    -DPICO_BOARD="${BOARD}"
    -DCMAKE_BUILD_TYPE=Release
)
[[ -n "${SSID}"       ]] && CMAKE_ARGS+=( -DWIFI_SSID="${SSID}" )
[[ -n "${PASS}"       ]] && CMAKE_ARGS+=( -DWIFI_PASSWORD="${PASS}" )
[[ -n "${CHANNEL}"    ]] && CMAKE_ARGS+=( -DWIFI_CHANNEL="${CHANNEL}" )
[[ -n "${FW_VERSION}" ]] && CMAKE_ARGS+=( -DFW_VERSION="${FW_VERSION}" )
(( FETCH_PICOTOOL )) && CMAKE_ARGS+=( -DPICOTOOL_FORCE_FETCH_FROM_GIT=ON )

info "SSID:     ${SSID:-<default: PicoBridge>}"
if [[ -n "${PASS}" ]]; then
    info "Password: (set, ${#PASS} chars)"
else
    warn "Password: <default: picobridge123> — published value, CHANGE IT with --pass"
fi
info "Channel:  ${CHANNEL:-<default: 6>}"
info "Board:    ${BOARD}"

info "Configuring (cmake)..."
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"

# ── Build ────────────────────────────────────────────────────────────────────
JOBS="$( (nproc 2>/dev/null) || (sysctl -n hw.ncpu 2>/dev/null) || echo 2 )"
info "Building (make -j${JOBS})..."
cmake --build "${BUILD_DIR}" -j"${JOBS}"

UF2="${BUILD_DIR}/picow_nic.uf2"
if [[ -f "${UF2}" ]]; then
    info "Done: ${UF2} ($(du -h "${UF2}" | cut -f1))"
    echo
    echo "  Flash it:   ./scripts/flash.sh \"${UF2}\""
else
    error "Build finished but ${UF2} was not produced."
    exit 1
fi
