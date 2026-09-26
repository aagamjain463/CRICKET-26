#!/bin/zsh
# Captures one stroke in slow motion from a fixed camera round the striker and tiles it into a contact sheet.
# Usage: Scripts/anim/stroke_qa.sh <label> <view> [capture args]   views: point (side-on, off side), front (from the
# bowler), leg (side-on, behind the batter), rear (keeper's view), close (arms, front three-quarter), back (arms, from
# behind square leg). Sheet: Saved/AnimQA/<label>_<view>.png
# Right-handers stand at (0.9, -0.35) in simulation metres, left-handers (-CricketLeftHanded) at (0.9, 0.35).
ROOT="${0:A:h:h:h}"
LABEL=$1; VIEW=$2; shift 2
typeset -A CAM
CAM[point]="1.25,6.5,1.0,1.25,-0.35,0.95,40"
CAM[front]="9,-0.35,1.25,0.9,-0.35,0.95,17"
CAM[leg]="1.25,-7,1.0,1.25,-0.35,0.95,40"
CAM[rear]="-3.2,-2.2,1.5,0.9,-0.35,0.9,34"
CAM[close]="7,2.4,1.4,0.9,-0.35,1.1,30"   # close front three-quarter from the off side, the arms filling the frame
CAM[back]="-2.5,-5.5,1.7,0.9,-0.35,1.1,30"  # close from behind square leg, over the back shoulder
CAM[lpoint]="1.25,-6.5,1.0,1.25,0.35,0.95,40" # a left-hander's point: pass -CricketLeftHanded
"$ROOT/Scripts/capture.sh" 1 -CricketSlowMo=0.25 -CricketShotEvery=0.04 -CricketDevCam=${CAM[$VIEW]} "$@" > /dev/null
grep -E "Display: HOM|Display: AWY|Pose:" "$ROOT/Saved/Capture.log" | sed 's/^.*Display: //'
# The frames round the stroke: live (phase 2) from 0.7 s before the contact to 1.5 s after (or the first 2 s when
# nothing was hit), every STEP-th (default 3, 0.12 s of game time apart).
read FIRST LAST <<< $(grep -o "Frame [0-9]* phase 2 time [0-9.]* contact [-0-9.]*" "$ROOT/Saved/Capture.log" | awk '
  { t=$6; c=$8; if (c <= 0) c = 0.8; if (t >= c - 0.7 && t <= c + 1.5) { if (f == "") f = $2; l = $2 } } END { print f, l }')
python3 "$ROOT/Scripts/anim/sheet.py" "$ROOT/Saved/AnimQA/${LABEL}_${VIEW}.png" $FIRST $LAST ${STEP:-3} ${CROP:-400,60,880,700}
