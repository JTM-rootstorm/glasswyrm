#!/usr/bin/env bash
set -euo pipefail

if (($# != 4)); then
  printf 'Usage: %s ARCHIVE SOURCE_DIRECTORY BUILD_DIRECTORY PREFIX\n' "$0" >&2
  exit 2
fi

archive=$1 source=$2 build=$3 prefix=$4
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
manifest=$here/clients.toml
signature=$archive.sig
for tool in cmake ninja pkg-config cc; do
  command -v "$tool" >/dev/null || { printf 'Missing required tool: %s\n' "$tool" >&2; exit 1; }
done
verify=(python3 "$here/verify_manifest.py" "$manifest" --archive "$archive")
[[ ! -f $signature ]] || verify+=(--signature "$signature")
"${verify[@]}"
rm -rf "$source" "$build" "$prefix"
mkdir -p "$source" "$build" "$prefix"
tar -xzf "$archive" -C "$source" --strip-components=1
python3 "$here/verify_manifest.py" "$manifest" --archive "$archive" --source-tree "$source"

cmake -S "$source" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=ON -DSDL_TESTS=ON \
  -DSDL_X11=ON -DSDL_X11_SHARED=OFF -DSDL_X11_XFIXES=ON \
  -DSDL_X11_XRANDR=ON -DSDL_X11_XCURSOR=OFF -DSDL_X11_XDBE=OFF \
  -DSDL_X11_XINPUT=OFF -DSDL_X11_XSCRNSAVER=OFF -DSDL_X11_XSHAPE=OFF \
  -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_OPENGL=OFF \
  -DSDL_OPENGLES=OFF -DSDL_VULKAN=OFF -DSDL_HIDAPI=OFF \
  -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF -DSDL_SENSOR=OFF \
  -DSDL_LIBUDEV=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
  -DSDL_DUMMYVIDEO=OFF -DSDL_OFFSCREEN=OFF -DSDL_RPI=OFF \
  -DSDL_VIVANTE=OFF -DSDL_ALSA=OFF -DSDL_JACK=OFF \
  -DSDL_PIPEWIRE=OFF -DSDL_PULSEAUDIO=OFF -DSDL_SNDIO=OFF \
  -DSDL_OSS=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF \
  -DSDL_FUSIONSOUND=OFF -DSDL_LIBSAMPLERATE=OFF -DSDL_DISKAUDIO=OFF \
  -DSDL_DUMMYAUDIO=ON
ninja -C "$build" SDL2 testdraw2 testsprite2
cmake --install "$build"

export PKG_CONFIG_PATH="$prefix/lib64/pkgconfig:$prefix/lib/pkgconfig"
cc -std=c11 -Wall -Wextra -Werror "$here/m12_sdl_probe.c" \
  -o "$prefix/bin/m12_sdl_probe" $(pkg-config --cflags --libs sdl2)
cc -std=c11 -Wall -Wextra -Werror \
  "$here/m12_xcb_probe.c" "$here/m12_xcb_graphics.c" \
  -o "$prefix/bin/m12_xcb_probe" \
  $(pkg-config --cflags --libs xcb xcb-shm xcb-xfixes xcb-damage xcb-render xcb-composite xcb-randr)
install -m 0755 "$build/test/testdraw2" "$build/test/testsprite2" "$prefix/bin/"
install -m 0644 "$source/test/icon.bmp" "$prefix/bin/icon.bmp"
printf 'M12 external clients built beneath %s\n' "$prefix"
