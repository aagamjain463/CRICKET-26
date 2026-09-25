#!/bin/zsh
# Cuts the batting reference video (a friend playing each shot, used with their permission) into one short clip per
# stroke for DeepMotion Animate 3D. Each clip runs from the stance through the contact to the follow-through, about
# two seconds, because the free tier's monthly credits pay for about a second of video each. The close side-on takes are upscaled whole;
# the far takes are cropped to the batter first so the batter fills the frame.
# Usage: Scripts/anim/cut_clips.sh [video] [out dir]
set -e
SRC="${1:-$HOME/Downloads/friend.mp4}"
OUT="${2:-$HOME/Downloads/mocap/clips}"
mkdir -p "$OUT"
CLOSE="scale=1280:720:flags=lanczos"
FAR="crop=240:240:180:50,scale=720:720:flags=lanczos"
# name      start   end     framing
CLIPS=(
  Defend_Front 14.6  16.6  $CLOSE
  Defend_Back  23.2  25.2  $CLOSE
  Drive        30.0  32.0  $CLOSE
  Drive_Cover  37.4  39.4  $CLOSE
  Punch        56.6  58.6  $CLOSE
  Pull         73.0  75.0  $CLOSE
  Sweep        81.6  83.6  $CLOSE
  Flick        110.3 112.3 $FAR
  Loft         139.3 141.3 $FAR
  Cut          156.9 158.9 $FAR
)
for NAME START END VF in $CLIPS; do
  ffmpeg -v error -y -ss $START -to $END -i "$SRC" -vf "$VF" -an -c:v libx264 -crf 16 -pix_fmt yuv420p "$OUT/Bat_$NAME.mp4"
done
ls -la "$OUT"
