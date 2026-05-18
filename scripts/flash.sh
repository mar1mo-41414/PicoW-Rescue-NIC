#!/usr/bin/env bash
# flash.sh — Flash PicoW-Rescue-NIC firmware to a Raspberry Pi Pico W
#
# Usage:
#   ./scripts/flash.sh              # auto-detect RPI-RP2 mount
#   ./scripts/flash.sh /path/to/uf2 # use specific .uf2 file
#
# Requirements:
#   - Pico W in BOOTSEL mode (hold button while plugging USB)
#   - Build already completed: mkdir build && cd build && cmake .. && make

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
UF2_FILE="${1:-${REPO_ROOT}/build/picow_nic.uf2}"

# ── Colors ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'  # No Color

info()  { echo -e "${GREEN}[flash]${NC} $*"; }
warn()  { echo -e "${YELLOW}[flash]${NC} $*"; }
error() { echo -e "${RED}[flash] ERROR:${NC} $*" >&2; }

# ── Check .uf2 exists ─────────────────────────────────────────────────────────
if [[ ! -f "${UF2_FILE}" ]]; then
    error "Firmware not found: ${UF2_FILE}"
    echo ""
    echo "  Build first:"
    echo "    mkdir -p build && cd build"
    echo "    cmake .. -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release"
    echo "    make -j\$(nproc)"
    exit 1
fi

info "Firmware: ${UF2_FILE} ($(du -h "${UF2_FILE}" | cut -f1))"

# ── Find the RPI-RP2 mount point ──────────────────────────────────────────────
find_mount() {
    if [[ "$(uname)" == "Darwin" ]]; then
        # macOS
        for d in /Volumes/RPI-RP2 /Volumes/RP2350; do
            [[ -d "$d" ]] && echo "$d" && return 0
        done
    else
        # Linux — try common mount patterns
        for d in \
            /media/"${USER}"/RPI-RP2 \
            /media/RPI-RP2 \
            /run/media/"${USER}"/RPI-RP2 \
            /mnt/RPI-RP2; do
            [[ -d "$d" ]] && echo "$d" && return 0
        done
        # fallback: scan /proc/mounts
        while IFS= read -r line; do
            mp=$(echo "$line" | awk '{print $2}')
            label=$(echo "$line" | awk '{print $1}')
            if [[ "${label}" == *"RPI-RP2"* ]] || [[ "${mp}" == *"RPI-RP2"* ]]; then
                echo "${mp}" && return 0
            fi
        done < /proc/mounts
    fi
    return 1
}

MOUNT_POINT=""
if MOUNT_POINT=$(find_mount 2>/dev/null); then
    info "Found RPI-RP2 at: ${MOUNT_POINT}"
else
    warn "RPI-RP2 drive not found."
    echo ""
    echo "  To enter BOOTSEL mode:"
    echo "    1. Unplug the Pico W"
    echo "    2. Hold the BOOTSEL button"
    echo "    3. Plug in the USB cable"
    echo "    4. Release the button"
    echo "    5. A drive named 'RPI-RP2' should appear"
    echo ""
    echo "  Then re-run this script."
    exit 1
fi

# ── Copy firmware ──────────────────────────────────────────────────────────────
info "Copying firmware to ${MOUNT_POINT}/ ..."
cp "${UF2_FILE}" "${MOUNT_POINT}/"

# The Pico unmounts itself automatically after receiving the UF2.
# Give it a moment, then confirm it's gone (optional).
sleep 1

if [[ -d "${MOUNT_POINT}" ]]; then
    warn "RPI-RP2 still mounted — Pico may not have rebooted yet."
    warn "Wait a few seconds and check that the Pico has restarted."
else
    info "Pico rebooted — firmware flashed successfully!"
    echo ""
    echo "  Next steps:"
    echo "    1. Check USB interface on host:  ip addr"
    echo "    2. Connect to WiFi AP:           SSID=PicoBridge, pass=picobridge123"
    echo "    3. Open debug console:           screen /dev/ttyACM0 115200"
fi
