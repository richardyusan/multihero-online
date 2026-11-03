#!/bin/bash
# Build MultiHero for Windows (x64) or Linux (x64) with SDL2 and Epic Online Services.
#
#   ./build_desktop.sh windows [path/to/MultiHero096]     -> dist/MultiHero-<ver>-windows-x64/ + .zip
#   ./build_desktop.sh linux   [path/to/MultiHero096]     -> dist/MultiHero-<ver>-linux-x64/ + .tar.gz
#
# Needs (Ubuntu/Debian):  g++  libsdl2-dev  g++-mingw-w64-x86-64  zip  python3-pil
#   SDL2_MINGW  SDL2 for MinGW (SDL2-devel-2.x-mingw.tar.gz from github.com/libsdl-org/SDL/releases),
#               the folder that contains x86_64-w64-mingw32/   (default /opt/sdl2-mingw/SDL2-2.30.9)
#   EOS_SDK     Epic Online Services SDK folder with Include/, Bin/, Lib/ (default third_party/eos/SDK,
#               filled by tools/setup_eos.py from the EOS C SDK zip). NO_EOS=1 builds without online.
#   CODE_ONLY=1 skip copying the game data (only build the program)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
TARGET="${1:-linux}"
GAME_DIR="${2:-$HERE/game}"
VERSION="${VERSION:-1.7.0}"
EOS_SDK="${EOS_SDK:-$HERE/third_party/eos/SDK}"
SDL2_MINGW="${SDL2_MINGW:-/opt/sdl2-mingw/SDL2-2.30.9}"
[ -n "$CODE_ONLY" ] || [ -f "$GAME_DIR/maps.dat" ] || { echo "Game data not found in '$GAME_DIR' (see game/README.md)"; exit 1; }

case "$TARGET" in
  windows) NAME="MultiHero-$VERSION-windows-x64" ;;
  linux)   NAME="MultiHero-$VERSION-linux-x64" ;;
  *) echo "usage: $0 windows|linux [game dir]"; exit 1 ;;
esac
OBJ="$HERE/build/desktop-$TARGET"
DIST="$HERE/dist/$NAME"
rm -rf "$OBJ" "$DIST"; mkdir -p "$OBJ" "$DIST"

echo "== 1. Blitz -> C++"
python3 "$HERE/tools/bb2cpp.py" "$HERE/src/mh.bb" "$OBJ/mh.cpp" "$HERE"/runtime/decls/*.decls

# -fwrapv: Blitz integers wrap; -ffp-contract=off: no fused multiply-add, so the float maths gives
# the same result on every CPU (online matches run the same simulation on every machine)
CFLAGS="-O2 -fwrapv -ffp-contract=off -w -I$HERE/runtime"
EOS_CFLAGS=""; EOS_LIBS=""
if [ "$TARGET" = windows ]; then
  CXX="x86_64-w64-mingw32-g++-posix"
  SDL="$SDL2_MINGW/x86_64-w64-mingw32"
  [ -f "$SDL/include/SDL2/SDL.h" ] || { echo "SDL2 for MinGW not found: set SDL2_MINGW"; exit 1; }
  CFLAGS="$CFLAGS -I$SDL/include/SDL2 -Dmain=SDL_main"
  LIBS="-L$SDL/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc -static-libstdc++"
  if [ -z "$NO_EOS" ] && [ -f "$EOS_SDK/Bin/EOSSDK-Win64-Shipping.dll" ]; then
    EOS_CFLAGS="-DMH_WITH_EOS -I$EOS_SDK/Include"
    EOS_LIBS="$EOS_SDK/Bin/EOSSDK-Win64-Shipping.dll"
    cp "$EOS_SDK/Bin/EOSSDK-Win64-Shipping.dll" "$DIST/"
  fi
  cp "$SDL/bin/SDL2.dll" "$DIST/"
  EXE="$DIST/MultiHero.exe"
  # program icon (from the Android launcher icon)
  python3 - "$HERE/android/res/mipmap-xxxhdpi/ic_launcher.png" "$OBJ/multihero.ico" <<'PY'
import sys
from PIL import Image
Image.open(sys.argv[1]).convert("RGBA").save(sys.argv[2], sizes=[(16,16),(32,32),(48,48),(64,64),(128,128),(256,256)])
PY
  printf '1 ICON "multihero.ico"\n' > "$OBJ/icon.rc"
  (cd "$OBJ" && x86_64-w64-mingw32-windres icon.rc -O coff -o icon.res.o)
  RES="$OBJ/icon.res.o"
  # the threads runtime of MinGW (needed by libstdc++'s std::thread / std::mutex)
  WP="$(ls /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll /usr/lib/gcc/x86_64-w64-mingw32/*/libwinpthread-1.dll 2>/dev/null | head -1)"
  [ -n "$WP" ] && cp "$WP" "$DIST/"
else
  CXX="g++"
  CFLAGS="$CFLAGS $(sdl2-config --cflags)"
  LIBS="$(sdl2-config --libs) -lpthread -Wl,-rpath,\$ORIGIN"
  if [ -z "$NO_EOS" ] && [ -f "$EOS_SDK/Bin/libEOSSDK-Linux-Shipping.so" ]; then
    EOS_CFLAGS="-DMH_WITH_EOS -I$EOS_SDK/Include"
    cp "$EOS_SDK/Bin/libEOSSDK-Linux-Shipping.so" "$DIST/"
    EOS_LIBS="-L$DIST -l:libEOSSDK-Linux-Shipping.so"
  fi
  # ship SDL2 too, so the game runs on systems without it
  SDLSO="$(ldconfig -p | awk '/libSDL2-2.0.so.0 .*x86-64/ {print $NF; exit}')"
  [ -n "$SDLSO" ] && cp -L "$SDLSO" "$DIST/libSDL2-2.0.so.0"
  EXE="$DIST/multihero"
  RES=""
fi
[ -n "$EOS_CFLAGS" ] && echo "   Epic Online Services: on" || echo "   Epic Online Services: off (SDK not found in $EOS_SDK)"

echo "== 2. compile ($TARGET)"
PIDS=""
for f in "$OBJ/mh.cpp" "$HERE/runtime/bbrt.cpp" "$HERE/runtime/mh_glue.cpp" "$HERE/runtime/desktop_main.cpp" "$HERE"/runtime/net/*.cpp; do
  $CXX -std=c++17 $CFLAGS $EOS_CFLAGS -c "$f" -o "$OBJ/$(basename "${f%.cpp}").o" & PIDS="$PIDS $!"
done
for p in $PIDS; do wait $p || { echo "compile failed"; exit 1; }; done

echo "== 3. link"
$CXX -o "$EXE" $(ls "$OBJ"/*.o | grep -v '\.res\.o$') $RES $EOS_LIBS $LIBS
[ "$TARGET" = linux ] && strip "$EXE" || x86_64-w64-mingw32-strip "$EXE" 2>/dev/null || true

if [ -z "$CODE_ONLY" ]; then
  echo "== 4. game data"
  python3 "$HERE/tools/prep_assets.py" "$GAME_DIR" "$DIST/assets" "$HERE/config"
fi
cp "$HERE/LICENSE" "$HERE/NOTICE.md" "$DIST/" 2>/dev/null || true
cat > "$DIST/README.txt" <<EOF
MultiHero $VERSION for $TARGET - port of MultiHero 0.96 by mhman
https://github.com/richardyusan/multihero-online

Start: $( [ "$TARGET" = windows ] && echo "MultiHero.exe" || echo "./multihero" )
Keep the 'assets' folder next to the program.

Keys (player 1): W A S D move, N attack, M jump, H special, Space block, J throw,
Enter OK, Esc menu. Alt+Enter: full screen. Change keys in Options > Controls.
Game pads: every pad is its own player (first pad used = player 1, second = player 2 ...).
A attack, B jump, X special, Y block, LB/RB throw, Start = OK, Back = Esc.

MULTIPLAYER ONLINE plays with Windows, Linux and Android players through Epic Online Services.
Saves and settings: $( [ "$TARGET" = windows ] && echo "%APPDATA%\\MultiHero" || echo "~/.local/share/MultiHero" )
(create an empty file portable.txt next to the program to keep them in ./save instead)
EOF
[ "$TARGET" = windows ] && sed -i 's/$/\r/' "$DIST/README.txt"

echo "== 5. package"
cd "$HERE/dist"
if [ -z "$CODE_ONLY" ]; then
  if [ "$TARGET" = windows ]; then rm -f "$NAME.zip"; zip -qr "$NAME.zip" "$NAME"; ls -la "$NAME.zip"
  else rm -f "$NAME.tar.gz"; tar czf "$NAME.tar.gz" "$NAME"; ls -la "$NAME.tar.gz"; fi
fi
echo "OK -> dist/$NAME"
