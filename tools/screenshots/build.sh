#!/bin/sh
# Renders README screenshots by running the real sketches on a PC.
#
# A small fake Arduino core (fake/) stands in for the board: time advances
# 33 ms per frame, buttons come from a per-game script (scripts/) that also
# plays the game with a simple autopilot and picks which frames to save,
# and the DHT11 always reads 17.2 C / 54 %. U8g2 renders into its normal
# memory buffer, which is saved and converted to PNG.
#
# Needs: gcc, g++, python3 with Pillow, and the U8g2 Arduino library.
# Usage: tools/screenshots/build.sh        (from anywhere)
set -e
H=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$H/../.." && pwd)
U8G2=${U8G2:-$HOME/Arduino/libraries/U8g2/src}
B=$H/build
mkdir -p "$B/obj" "$B/pbm"

if [ ! -f "$B/libu8g2.a" ]; then
  echo "building U8g2..."
  (cd "$B/obj" && gcc -O1 -c "$U8G2"/clib/*.c)
  ar rcs "$B/libu8g2.a" "$B"/obj/*.o
fi

for game in cave_flyer kitty_pounce pocket_kitty dht_test; do
  g++ -std=gnu++17 -O1 -w -I"$H/fake" -I"$U8G2" -I"$REPO/libraries/NanoOledKit/src" \
      -DGAME_INO="\"$REPO/$game/$game.ino\"" -DSCRIPT="\"$H/scripts/$game.h\"" \
      "$H/harness.cpp" "$B/libu8g2.a" -o "$B/run_$game"
  "$B/run_$game" "$B/pbm"
done

python3 -I "$H/topng.py" "$B/pbm" "$REPO/docs/screenshots"
