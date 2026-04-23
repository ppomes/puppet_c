#!/bin/bash
# Build a timestamped dev .deb of puppet_c.
# Version format: 0.0.1-YYYYMMDDhhmm (SQL-style timestamp, monotonic for reprepro).
#
# Usage: ./scripts/dev-build.sh [--install]
set -e

cd "$(dirname "$0")/.."

VERSION="0.0.1-$(date +%Y%m%d%H%M)"
MSG="${DCH_MSG:-Dev build $VERSION}"

DEBEMAIL="${DEBEMAIL:-pierre.pomes@gmail.com}" \
DEBFULLNAME="${DEBFULLNAME:-Pierre POMES}" \
  dch -v "$VERSION" "$MSG" -D unstable >/dev/null

echo "Bumped debian/changelog → $VERSION"
dpkg-buildpackage -b -us -uc 2>&1 | tail -3

echo
echo "Built packages:"
ls -la "../"*"_${VERSION}_"*.deb 2>/dev/null | awk '{print "  "$NF}'

if [ "${1:-}" = "--install" ]; then
  echo
  echo "Installing runtime packages locally..."
  sudo dpkg -i \
    "../libfacter-c0_${VERSION}_amd64.deb" \
    "../libpuppetc-common0_${VERSION}_amd64.deb" \
    "../libpuppetc0_${VERSION}_amd64.deb" \
    "../puppetc_${VERSION}_amd64.deb"
fi

echo
echo "To publish on reprepro:"
echo "  reprepro includedeb itrepo2 ../*_${VERSION}_*.deb"

# Auto-revert changelog so the working tree stays clean for future commits.
# The .deb artifacts in ../ remain intact.
git checkout -- debian/changelog
echo "debian/changelog reverted (artifacts kept in ../)."
