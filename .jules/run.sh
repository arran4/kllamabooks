#!/bin/bash
set -euo pipefail

ROOTFS_DIR="${HOME}/.cache/kde-dev-rootfs/rootfs"

if [ ! -d "${ROOTFS_DIR}" ]; then
    echo "Rootfs not found at ${ROOTFS_DIR}. Did you run .jules/bootstrap.sh?"
    return 1 2>/dev/null || false
fi

export QT_QPA_PLATFORM=offscreen

# We must use chroot as requested. We will setup mounts and cleanup via traps.
# Only root can chroot and mount.
# But we must run the target command as the user. Let's do it right.

TARGET_DIR="/workspace"

setup_mounts() {
    sudo mount -t proc proc "${ROOTFS_DIR}/proc"
    sudo mount -t sysfs sys "${ROOTFS_DIR}/sys"
    sudo mount --bind /dev "${ROOTFS_DIR}/dev"
    sudo mount --bind /dev/pts "${ROOTFS_DIR}/dev/pts"
    sudo mount --bind /etc/resolv.conf "${ROOTFS_DIR}/etc/resolv.conf"

    sudo mkdir -p "${ROOTFS_DIR}${TARGET_DIR}"
    sudo mount --bind "$(pwd)" "${ROOTFS_DIR}${TARGET_DIR}"
}

cleanup_mounts() {
    sudo umount "${ROOTFS_DIR}${TARGET_DIR}" || true
    sudo umount "${ROOTFS_DIR}/etc/resolv.conf" || true
    sudo umount "${ROOTFS_DIR}/dev/pts" || true
    sudo umount "${ROOTFS_DIR}/dev" || true
    sudo umount "${ROOTFS_DIR}/sys" || true
    sudo umount "${ROOTFS_DIR}/proc" || true
}

trap cleanup_mounts EXIT
setup_mounts

# Install libsqlcipher-dev if missing
sudo chroot "${ROOTFS_DIR}" /bin/bash -c "dpkg -l | grep -q libsqlcipher-dev || (apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y libsqlcipher-dev)"

# Run command inside chroot. Since we run as root, we should drop privileges.
# But for now let's just run it as root because that's what chroot defaults to.
# Or we can run it as the host user uid if it exists, or just let it run.
sudo chroot --userspec=$(id -u):$(id -g) "${ROOTFS_DIR}" /bin/bash -c "cd ${TARGET_DIR} && export QT_QPA_PLATFORM=offscreen && \"\$@\"" _ "$@"
