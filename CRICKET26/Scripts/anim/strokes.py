# Key poses for each batting stroke, authored for a right-handed batter (author_stroke.py bakes them).
#
# Batter frame, metres, origin between the feet at the stance: f toward the bowler, o toward the off side, z up.
# Every channel is keyed at the stance; later keys list only what changes. Times are seconds.
#   bat_f/o/z   the grip centre                  bat_th  swing: 0 toe straight down, -90 toe at the keeper,
#   bat_ps      direction of the swing (deg,             +90 toe at the bowler, 180 toe straight up
#               + toward the off side)           bat_ph  face roll about the handle, + opens toward the off side
#   hip_f/o, drop   pelvis offset and lowering   hip_yaw/tilt/side, ch_yaw/bend/side: turn toward the bowler,
#   footX_f/o   ball of the foot (planted: held)          bend forward, lean toward the bowler (deg)
#   footX_yaw   toe turned toward the bowler     footX_heel  heel raised about the ball (deg); footX_lift  (m)
#   poleX_f/o/z where the elbow points, in the chest's frame   clavX_up/fwd  shoulder girdle (deg)
#   look_f/o/z  what the eyes follow
# X is L (front, top hand) or R (back, bottom hand).

# The grip: each palm's roll round the handle from square to the face (deg), fixed for the whole stroke as a real
# grip is. At 60 the top palm faces the batter and the back of the top glove cover, both Vs down the handle between
# the splice and the outside edge. Calibrated with the arm cost over the golden drive's stance, backlift and contact.
GRIP_ROLL = {'l': 60.0, 'r': 60.0}

STANCE = dict(
    bat_f=0.05, bat_o=0.40, bat_z=0.75, bat_th=-40, bat_ps=0, bat_ph=-20,
    hip_f=0.0, hip_o=0.02, drop=0.10, hip_yaw=8, hip_tilt=16, hip_side=0,
    ch_yaw=14, ch_bend=32, ch_side=0,
    footL_f=0.24, footL_o=0.14, footL_yaw=10, footL_heel=0, footL_lift=0,
    footR_f=-0.24, footR_o=0.14, footR_yaw=-5, footR_heel=0, footR_lift=0,
    poleL_f=0.6, poleL_o=0.2, poleL_z=-0.6, poleR_f=-0.3, poleR_o=0.1, poleR_z=-1.0,
    clavL_up=0, clavL_fwd=10, clavR_up=0, clavR_fwd=8,
    look_f=20.0, look_o=0.0, look_z=1.0,
)

# The golden straight drive to a full ball on off stump. Contact (0.82 s, frame 50 at 60 fps) beside the front
# pad, under the eyes, bat near vertical and the handle ahead; then a high, balanced finish.
STRAIGHT_DRIVE = [
    (0.00, STANCE),
    (0.18, dict(bat_th=-45, bat_ph=-20, footR_f=-0.24, footR_o=0.14, footR_lift=0.0)),
    # Trigger: the back foot steps a little back and across (lifted, never slid), the bat rising.
    (0.24, dict(footR_lift=0.03)),
    (0.30, dict(footR_f=-0.28, footR_o=0.18, bat_f=-0.05, bat_o=0.55, bat_z=0.95, bat_th=-80, bat_ps=-20, bat_ph=50, hip_o=0.04,
                look_f=10.0, look_z=0.6)),
    # The front foot lifts.
    (0.36, dict(footL_lift=0.0, footL_f=0.24, hip_f=0.02)),
    (0.48, dict(footL_lift=0.07, footL_f=0.46, footL_o=0.18, footL_yaw=24, hip_f=0.16, drop=0.09)),
    # Top of the backlift as the front foot lands, pointing at the ball's line.
    (0.58, dict(bat_f=0.0, bat_o=0.40, bat_z=1.20, bat_th=-135, bat_ps=-6, bat_ph=35,
                poleL_f=0.8, poleL_o=0.5, poleL_z=0.1, poleR_f=-0.4, poleR_o=-0.2, poleR_z=-0.9,
                ch_yaw=6, ch_bend=22, hip_yaw=4)),
    (0.62, dict(footL_lift=0.0, footL_f=0.70, footL_o=0.22, footL_yaw=36, hip_f=0.28, drop=0.13,
                hip_tilt=14, ch_side=4)),
    # Downswing: the front shoulder and top elbow lead, the handle ahead.
    (0.74, dict(bat_f=0.36, bat_o=0.50, bat_z=1.02, bat_th=-75, bat_ps=0, bat_ph=50,
                poleL_f=1.0, poleL_o=0.3, poleL_z=0.3, poleR_f=-0.2, poleR_o=0.0, poleR_z=-1.0,
                hip_f=0.38, drop=0.15, ch_yaw=14, ch_bend=28, ch_side=10, look_f=1.2, look_o=0.4, look_z=0.3,
                footR_heel=10)),
    # The hands lead the blade down, the face still open, the bottom hand behind the handle. The bat turns at an
    # even rate into contact: bunched later, the bottom arm would have to turn faster than an arm can.
    (0.78, dict(bat_f=0.72, bat_o=0.55, bat_z=0.83, bat_th=-44, bat_ph=30)),
    (0.80, dict(bat_f=0.86, bat_o=0.50, bat_z=0.79, bat_th=-30, bat_ph=20)),
    # Contact: the sweet spot (grip + 0.55 m down the handle) at (0.80, 0.44, 0.30), beside and just ahead of the
    # front toe under the eyes, the handle 18 deg ahead; the chest bent over it and the back shoulder driven through
    # (short of that, the bottom elbow is caught between the trunk and the limit of its wrist).
    (0.82, dict(bat_f=0.80 + 0.55 * 0.309, bat_o=0.44, bat_z=0.30 + 0.55 * 0.951, bat_th=-18, bat_ph=0,
                hip_f=0.45, drop=0.22, hip_yaw=20, hip_tilt=20, ch_yaw=32, ch_bend=46, ch_side=6, clavR_fwd=40,
                poleL_f=1.0, poleL_o=0.2, poleL_z=0.5, poleR_f=-0.1, poleR_o=0.2, poleR_z=-1.0,
                footR_heel=35, look_f=0.80, look_o=0.44, look_z=0.3)),
    # Through the line of the ball, the hands still speeding up the blade toward the bowler and the chest opening
    # after them, so the bottom elbow clears the trunk.
    (0.84, dict(bat_f=1.05, bat_o=0.45, bat_z=0.95, bat_th=0, bat_ph=20)),
    (0.87, dict(bat_f=1.10, bat_o=0.45, bat_z=1.10, bat_th=15, bat_ph=35, ch_yaw=40, hip_yaw=26, clavR_fwd=44)),
    (0.90, dict(bat_f=1.15, bat_o=0.40, bat_z=1.20, bat_th=30, bat_ph=20)),
    (0.96, dict(hip_f=0.46, ch_yaw=44, ch_bend=38,
                poleL_f=0.7, poleL_o=0.6, poleL_z=0.6, footR_heel=35, footR_lift=0.0, footR_f=-0.28, footR_o=0.18, footR_yaw=-5, look_f=6.0, look_o=0.2, look_z=0.4)),
    (0.99, dict(ch_yaw=46, clavR_fwd=36)),
    # The back foot, dragged onto its toe, lifts and steps through to take the weight: it never slides.
    (1.10, dict(footR_lift=0.07, footR_f=-0.12, footR_o=0.20, footR_yaw=15, ch_yaw=50, hip_yaw=35, ch_bend=38)),
    # A high finish: hands above the head in front of the front shoulder, the toe up and the face turned to the off
    # side, as far back as both wrists allow with the grip unchanged.
    (1.22, dict(bat_f=0.80, bat_o=0.50, bat_z=1.70, bat_th=140, bat_ps=10, bat_ph=100,
                poleL_f=0.3, poleL_o=0.6, poleL_z=0.9, poleR_f=0.5, poleR_o=0.4, poleR_z=-0.4,
                hip_f=0.50, drop=0.17, hip_yaw=30, ch_yaw=45, ch_bend=22, ch_side=8, clavR_fwd=20,
                footR_lift=0.0, footR_f=0.02, footR_o=0.22, footR_yaw=30, footR_heel=25, look_f=20.0, look_o=0.0, look_z=1.0)),
    (1.50, dict(bat_f=0.78, bat_o=0.48, bat_z=1.66, bat_th=130, bat_ps=0, bat_ph=100)),
    # Recover: weight back between the feet, the finish relaxed to shoulder height, balanced. (The bat is not
    # brought down in front here: that needs the bottom forearm turned the other way, the game's blend to the
    # stance does it.)
    (1.90, dict(bat_f=0.70, bat_o=0.50, bat_z=1.50, bat_th=135, bat_ps=-20, bat_ph=105,
                poleL_f=0.6, poleL_o=0.3, poleL_z=-0.5, poleR_f=-0.2, poleR_o=0.2, poleR_z=-1.0,
                hip_f=0.34, drop=0.10, hip_yaw=14, hip_tilt=10, ch_yaw=20, ch_bend=16, ch_side=0, footR_heel=20)),
]

KEYS = {'StraightDrive': STRAIGHT_DRIVE}
CONTACT = {'StraightDrive': 0.82}
