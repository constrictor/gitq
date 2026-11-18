#!/bin/bash
# Build, test and package gitq on Ubuntu (used by CI inside an ubuntu:<version> container).
# Produces dist/gitq_<version>-ubuntu<release>_<arch>.deb and dist/gitq-<version>-ubuntu<release>-<arch>.AppImage.
set -euxo pipefail

SRC=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"

export DEBIAN_FRONTEND=noninteractive
if [ "${SKIP_DEPS:-0}" != 1 ]; then
    apt-get update
    apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build pkg-config file dpkg-dev git ca-certificates wget \
        qt6-base-dev qt6-base-dev-tools libgit2-dev \
        qt6-qpa-plugins libgl-dev fonts-dejavu-core desktop-file-utils
fi

. /etc/os-release
ARCH=$(dpkg --print-architecture)
APPARCH=$(uname -m)
VERSION=$(sed -n 's/^project(gitq VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
SUFFIX="ubuntu${VERSION_ID}"

# git is used by the test fixtures
git config --global user.name  >/dev/null || git config --global user.name  "CI"
git config --global user.email >/dev/null || git config --global user.email "ci@example.invalid"
git config --global init.defaultBranch >/dev/null || git config --global init.defaultBranch master

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure

mkdir -p dist

# .deb
(cd build && cpack -G DEB)
mv build/gitq_*.deb "dist/gitq_${VERSION}-${SUFFIX}_${ARCH}.deb"
dpkg-deb --info "dist/gitq_${VERSION}-${SUFFIX}_${ARCH}.deb"

# AppImage (linuxdeploy + Qt plugin; extract-and-run because containers have no FUSE)
export APPIMAGE_EXTRACT_AND_RUN=1
TOOLS="$SRC/build/tools"
mkdir -p "$TOOLS"
for t in linuxdeploy linuxdeploy-plugin-qt; do
    [ -x "$TOOLS/$t-$APPARCH.AppImage" ] || \
        wget -q -O "$TOOLS/$t-$APPARCH.AppImage" \
            "https://github.com/linuxdeploy/$t/releases/download/continuous/$t-$APPARCH.AppImage"
    chmod +x "$TOOLS/$t-$APPARCH.AppImage"
done
rm -rf build/AppDir
DESTDIR="$SRC/build/AppDir" cmake --install build
export QMAKE=$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake)
export LDAI_OUTPUT="dist/gitq-${VERSION}-${SUFFIX}-${APPARCH}.AppImage"
export PATH="$TOOLS:$PATH"
"$TOOLS/linuxdeploy-$APPARCH.AppImage" --appdir build/AppDir \
    --desktop-file build/AppDir/usr/share/applications/gitq.desktop \
    --icon-file build/AppDir/usr/share/icons/hicolor/scalable/apps/gitq.svg \
    --plugin qt --output appimage
"$LDAI_OUTPUT" --version

ls -l dist
