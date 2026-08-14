#!/bin/bash
set -euo pipefail

ROOTFS_URL="https://github.com/arran4/kde-dev-rootfs/releases/latest/download/kde-dev-rootfs-forky-amd64.tar.zst"
SHA256_URL="https://github.com/arran4/kde-dev-rootfs/releases/latest/download/kde-dev-rootfs-forky-amd64.tar.zst.sha256"

CACHE_DIR="${HOME}/.cache/kde-dev-rootfs"
MARKER_FILE="${CACHE_DIR}/.ready"
ARCHIVE_PATH="${CACHE_DIR}/kde-dev-rootfs.tar.zst"
ROOTFS_DIR="${CACHE_DIR}/rootfs"

if [ -f "${MARKER_FILE}" ]; then
    echo "Rootfs already downloaded and extracted."
else
    mkdir -p "${CACHE_DIR}"

    echo "Downloading rootfs archive..."
    curl --fail --location --retry 3 -o "${ARCHIVE_PATH}" "${ROOTFS_URL}"

    echo "Downloading checksum..."
    curl --fail --location --retry 3 -o "${ARCHIVE_PATH}.sha256" "${SHA256_URL}"

    echo "Verifying checksum..."
    EXPECTED_SHA=$(cat "${ARCHIVE_PATH}.sha256" | awk '{print $1}')
    ACTUAL_SHA=$(sha256sum "${ARCHIVE_PATH}" | awk '{print $1}')

    if [ "${EXPECTED_SHA}" != "${ACTUAL_SHA}" ]; then
        echo "Checksum mismatch! Expected: ${EXPECTED_SHA}, Actual: ${ACTUAL_SHA}"
        return 1 2>/dev/null || false
    fi

    echo "Extracting rootfs..."
    mkdir -p "${ROOTFS_DIR}"
    sudo tar --use-compress-program=zstd -xf "${ARCHIVE_PATH}" -C "${ROOTFS_DIR}" --numeric-owner

    touch "${MARKER_FILE}"
    echo "Rootfs bootstrap complete."
fi
