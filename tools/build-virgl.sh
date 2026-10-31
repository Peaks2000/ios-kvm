#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/prepare-virgl-mesa.py
if [ ! -f .local/mesa-virgl-build/build.ninja ]; then
    meson setup .local/mesa-virgl-build .local/mesa-25.0.7 \
      --cross-file .local/mesa-virgl-cross.ini --wrap-mode=nofallback --buildtype=release \
      -Dplatforms= -Dgallium-drivers=softpipe,virgl -Dvulkan-drivers= \
      -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dosmesa=true -Dllvm=disabled \
      -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
      -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled \
      -Dvalgrind=disabled -Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= \
      -Dgallium-vdpau=disabled -Dgallium-va=disabled -Dgallium-xa=disabled \
      -Dlmsensors=disabled -Dbuild-tests=false
fi
ninja -C .local/mesa-virgl-build -j "${BUILD_JOBS:-4}" src/gallium/targets/osmesa/libOSMesa.8.dylib
