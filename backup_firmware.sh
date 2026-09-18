#!/bin/bash
# =============================================================================
# LilyGO T-Display-S3 - Firmware Backup Script
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BACKUP_DIR="${SCRIPT_DIR}/backup"
FLASH_SIZE="0x1000000"  # 16 MB
BAUD_RATE="921600"
CHIP="esp32s3"

mkdir -p "${BACKUP_DIR}"

if [ $# -ge 1 ]; then
    PORT="$1"
else
    PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial-* /dev/cu.wchusbserial* 2>/dev/null | head -n1 || true)
    if [ -z "${PORT}" ]; then
        echo "ERROR: No USB serial device found."
        echo "Please connect the LilyGO T-Display-S3 and try again."
        exit 1
    fi
    echo "Auto-detected serial port: ${PORT}"
fi

TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
BACKUP_FILE="${BACKUP_DIR}/firmware_backup_${TIMESTAMP}.bin"

# Locate esptool
ESPTOOL="${HOME}/.platformio/penv/bin/esptool.py"
if [ ! -f "${ESPTOOL}" ]; then
    ESPTOOL="esptool.py"
fi

echo "============================================="
echo "  LilyGO T-Display-S3 - Firmware Backup"
echo "============================================="
echo "  Port:    ${PORT}"
echo "  Chip:    ${CHIP}"
echo "  Size:    16 MB (${FLASH_SIZE})"
echo "  Output:  ${BACKUP_FILE}"
echo "============================================="
echo ""
echo "Reading entire flash (16MB)..."
echo "If connection hangs at 'Connecting...', hold BOOT (GPIO 0), press RST, release BOOT."
echo ""

python3 "${ESPTOOL}" --chip "${CHIP}" --port "${PORT}" --baud "${BAUD_RATE}" \
    read_flash 0x0 "${FLASH_SIZE}" "${BACKUP_FILE}"

if [ -f "${BACKUP_FILE}" ]; then
    FILE_SIZE=$(stat -f%z "${BACKUP_FILE}" 2>/dev/null || stat -c%s "${BACKUP_FILE}" 2>/dev/null)
    echo ""
    echo "============================================="
    echo "  Backup complete!"
    echo "============================================="
    echo "  File: ${BACKUP_FILE}"
    echo "  Size: ${FILE_SIZE} bytes"
    echo "============================================="
else
    echo "ERROR: Backup file was not created!"
    exit 1
fi
