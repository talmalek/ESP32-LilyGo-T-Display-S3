#!/bin/bash
# =============================================================================
# LilyGO T-Display-S3 - Firmware Restore Script
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BAUD_RATE="921600"
CHIP="esp32s3"

if [ $# -lt 1 ]; then
    echo "Usage: $0 <backup_file.bin> [serial_port]"
    echo ""
    echo "Available backups:"
    ls -lh "${SCRIPT_DIR}/backup/"*.bin 2>/dev/null || echo "  (no backups found in ${SCRIPT_DIR}/backup/)"
    exit 1
fi

BACKUP_FILE="$1"
if [ ! -f "${BACKUP_FILE}" ]; then
    echo "ERROR: File not found: ${BACKUP_FILE}"
    exit 1
fi

if [ $# -ge 2 ]; then
    PORT="$2"
else
    PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -n1 || true)
    if [ -z "${PORT}" ]; then
        echo "ERROR: No USB serial device found."
        exit 1
    fi
    echo "Auto-detected serial port: ${PORT}"
fi

ESPTOOL="${HOME}/.platformio/penv/bin/esptool.py"
if [ ! -f "${ESPTOOL}" ]; then
    ESPTOOL="esptool.py"
fi

echo "============================================="
echo "  LilyGO T-Display-S3 - Firmware Restore"
echo "============================================="
echo "  Port:    ${PORT}"
echo "  Chip:    ${CHIP}"
echo "  File:    ${BACKUP_FILE}"
echo "============================================="
echo ""
echo "Flashing backup binary to 0x0..."
python3 "${ESPTOOL}" --chip "${CHIP}" --port "${PORT}" --baud "${BAUD_RATE}" \
    write_flash 0x0 "${BACKUP_FILE}"

echo ""
echo "============================================="
echo "  Restore complete! Please reset your device."
echo "============================================="
