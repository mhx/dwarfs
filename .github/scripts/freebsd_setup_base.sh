#!/bin/sh
set -euo pipefail

# ===== Config =====
FREEBSD_REL="${FREEBSD_REL:-14.3-RELEASE}"
FREEBSD_VER_TAG="${FREEBSD_VER_TAG:-14_3}"

SNAP_LABEL="${SNAP_LABEL:-base-$(date +%Y%m%d)}"

ZPOOL="${ZPOOL:-zroot}"
JAILS_DS="${JAILS_DS:-${ZPOOL}/z/jails}"
JAILS_MP="${JAILS_MP:-/z/jails}"

PKG_REPO_URL='pkg+http://pkg.FreeBSD.org/${ABI}/latest'

PKGS="boost-all brotli ccache cmake date double-conversion flac fuse fusefs-libs fusefs-libs3 glog libarchive liblz4 mold ninja nlohmann-json openssl parallel-hashmap pkgconf range-v3 utf8cpp xxhash zstd ca_root_nss git"

# ===== Layout =====
sudo zfs list -H "${ZPOOL}" >/dev/null 2>&1 || { echo "No pool ${ZPOOL}"; exit 1; }
sudo zfs create -o mountpoint=/z       "${ZPOOL}/z"       >/dev/null 2>&1 || true
sudo zfs create -o mountpoint=/z/jails "${JAILS_DS}"      >/dev/null 2>&1 || true

BASE_DS="${JAILS_DS}/${FREEBSD_VER_TAG}"
BASE_MP="${JAILS_MP}/${FREEBSD_VER_TAG}"

if ! sudo zfs list -H "${BASE_DS}" >/dev/null 2>&1; then
  sudo zfs create -o mountpoint="${BASE_MP}" "${BASE_DS}"
fi

# ===== Fetch & extract base.txz =====
if [ ! -f "${BASE_MP}/.seeded" ]; then
  TMPDIR="$(mktemp -d)"
  trap 'rm -rf "$TMPDIR"' EXIT
  fetch -o "${TMPDIR}/base.txz" "https://download.freebsd.org/releases/amd64/${FREEBSD_REL}/base.txz"
  sudo tar -xpf "${TMPDIR}/base.txz" -C "${BASE_MP}"
  sudo touch "${BASE_MP}/.seeded"
fi

# ===== Provide networking config to the chroot =====
sudo mkdir -p "${BASE_MP}/etc"
# copy DNS + name service config from host so pkg can resolve names
sudo cp /etc/resolv.conf "${BASE_MP}/etc/resolv.conf"
[ -f /etc/hosts ]      && sudo cp /etc/hosts      "${BASE_MP}/etc/hosts"
[ -f /etc/nsswitch.conf ] && sudo cp /etc/nsswitch.conf "${BASE_MP}/etc/nsswitch.conf"

# ===== Preinstall deps inside the tree =====
sudo mount -t devfs   devfs "${BASE_MP}/dev"
sudo mount -t fdescfs fdesc "${BASE_MP}/dev/fd"
sudo mount -t procfs  proc  "${BASE_MP}/proc"

sudo chroot "${BASE_MP}" /bin/sh -lc "
  set -e
  env ASSUME_ALWAYS_YES=yes pkg bootstrap -f
  mkdir -p /usr/local/etc/pkg/repos
  echo 'FreeBSD: { url: \"${PKG_REPO_URL}\" }' > /usr/local/etc/pkg/repos/FreeBSD.conf
  pkg update
  pkg install -y ${PKGS}
  mkdir -p /root/.ccache
"

sudo umount -f "${BASE_MP}/proc"    || true
sudo umount -f "${BASE_MP}/dev/fd"  || true
sudo umount -f "${BASE_MP}/dev"     || true

# ===== Snapshot =====
sudo zfs snapshot "${BASE_DS}@${SNAP_LABEL}"
echo "Snapshot created: ${BASE_DS}@${SNAP_LABEL}"
