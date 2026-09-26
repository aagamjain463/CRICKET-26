# Key poses of each bowling action, authored for a right-arm bowler (author_bowl.py bakes them; mirror() gives the
# left-armer). Keyed off the broadcast in shot.mp4: the right-arm quick (#14, 667.4-669.1 s) and the right-arm off
# spinner (#9, 419.4-421.4 s), frame by frame at 30 fps.
#
# Bowler frame, metres: f along the run toward the batter, r toward the bowling arm's side, z up. The origin is the
# pelvis at the release, on the ground. Times are seconds from the release; every clip runs from START to END.
# Each channel is keyed on its own times, as (t, value) pairs; Blender's F-curves interpolate between them.
#
#   hip_f/r/z        pelvis place (z: up from the standing height)
#   hip_yaw/bend/side, ch_yaw/bend/side   pelvis and chest: turned toward the bowling arm, bent forward, leaning
#                    toward the bowling arm (deg). A right-armer side-on has the chest turned about 70 to the right.
#   footX_f/r        ball of the foot (planted: held)     footX_lift (m), footX_yaw (toe toward the arm side, deg),
#   footX_heel       heel raised about the ball (deg)
#   armX_a           the shoulder-to-wrist line round the shoulder in the run's vertical plane: 0 hanging, 90 ahead,
#                    180 straight up, -90 behind. Keyed unwrapped: the bowling arm runs down through -190 at release.
#   armX_p           that plane swung out to the arm's own side (deg; negative across the body)
#   armX_reach       shoulder-to-wrist over the arm's length (1: straight)
#   armX_pole_f/r/z  where the elbow points          handX_palm_f/r/z  where the palm faces
#   handX_flex       wrist flexion (deg, + toward the palm)      clavX_up  shoulder girdle raised (deg)
#   look_f/r/z       where the eyes stay: on the batter, for the whole action
# X is R (the bowling arm, the back foot at the delivery stride) or L (the front arm and the front foot).
START, END = -1.0, 1.0


def k(*pairs):
    return [(pairs[i], pairs[i + 1]) for i in range(0, len(pairs), 2)]


# The eyes hold the batter at the far end (the striker stands 18.5 m ahead and half a metre to the arm's side of
# a bowler running in over the wicket) the whole way: in the video the head neither bobs nor turns off target.
LOOK = dict(look_f=k(START, 18.5), look_r=k(START, 0.5), look_z=k(START, 1.3))

PACE = dict(
    LOOK,
    # About 5.5 m/s into the bound and through the air, braking on the back foot and harder on the front leg, then
    # running on. The stride fits the players' legs: 1.3 m ball to ball, the pelvis sinking into it.
    hip_f=k(-1.0, -4.14, -0.84, -3.24, -0.70, -2.47, -0.47, -1.32, -0.17, -0.42, 0.0, 0.0, 0.10, 0.22, 0.20, 0.45, 0.35, 0.85, 0.60, 1.55, 0.85, 2.15, 1.0, 2.50),
    hip_r=k(-1.0, 0.0, -0.17, 0.0, 0.3, -0.06, 1.0, -0.45),
    hip_z=k(-1.0, -0.06, -0.92, -0.04, -0.84, -0.08, -0.76, -0.05, -0.70, -0.03, -0.58, 0.08, -0.47, -0.10, -0.30, -0.14, -0.17, -0.16, 0.0, -0.03, 0.15, -0.18, 0.35, -0.14, 0.62, -0.08, 0.85, -0.06, 1.0, -0.05),
    hip_yaw=k(-1.0, 0.0, -0.70, 5.0, -0.58, 25.0, -0.47, 45.0, -0.17, 35.0, 0.0, 0.0, 0.15, -25.0, 0.35, -30.0,
              0.60, -10.0, 1.0, 0.0),
    hip_bend=k(-1.0, 10.0, -0.47, 5.0, -0.17, 8.0, 0.0, 15.0, 0.15, 35.0, 0.30, 25.0, 0.60, 12.0),
    hip_side=k(-1.0, 0.0),
    # Side-on at the back foot (front shoulder at the batter), coiled until the front foot is down, then the chest
    # whips through square to face past the batter as the trunk goes over the front leg, nearly flat.
    ch_yaw=k(-1.0, 0.0, -0.70, 10.0, -0.58, 45.0, -0.47, 70.0, -0.25, 72.0, -0.12, 45.0, 0.0, 5.0, 0.08, -35.0,
             0.18, -60.0, 0.35, -50.0, 0.60, -20.0, 1.0, -5.0),
    ch_bend=k(-1.0, 12.0, -0.70, 8.0, -0.58, 4.0, -0.47, 0.0, -0.25, -5.0, -0.17, 0.0, -0.05, 15.0, 0.0, 30.0,
              0.08, 55.0, 0.15, 72.0, 0.25, 68.0, 0.40, 45.0, 0.60, 20.0, 1.0, 12.0),
    ch_side=k(-1.0, 0.0, -0.47, 10.0, -0.25, 12.0, -0.12, 0.0, 0.0, -25.0, 0.10, -20.0, 0.30, 0.0),
    clavR_up=k(-1.0, 0.0, -0.12, 0.0, -0.03, 20.0, 0.05, 15.0, 0.18, 0.0),
    clavL_up=k(-1.0, 0.0, -0.47, 5.0, -0.33, 20.0, -0.12, 15.0, 0.0, 0.0),
    # Feet: the last running steps, the left foot's take-off, the bound with the left knee high, the back foot
    # landing side-on under the body, the long delivery stride to the braced front foot, then the back leg through.
    footR_f=k(-1.0, -4.45, -0.97, -4.45, -0.90, -4.20, -0.80, -3.50, -0.70, -2.80, -0.58, -1.85, -0.50, -1.22, -0.47, -1.12, -0.18, -1.12, -0.10, -0.95, 0.0, -0.55, 0.10, 0.0, 0.20, 0.55, 0.30, 1.05, 0.36, 1.20, 0.60, 1.20, 0.70, 1.70, 0.80, 2.25, 0.88, 2.55, 1.0, 2.55),
    footR_r=k(-1.0, 0.10, -0.47, 0.08, -0.18, 0.08, 0.36, 0.05, 0.60, 0.05, 0.88, -0.25, 1.0, -0.25),
    footR_lift=k(-1.0, 0.0, -0.97, 0.0, -0.90, 0.20, -0.80, 0.35, -0.70, 0.40, -0.58, 0.30, -0.50, 0.06, -0.47, 0.0, -0.18, 0.0, -0.10, 0.03, 0.0, 0.06, 0.10, 0.20, 0.20, 0.30, 0.30, 0.10, 0.36, 0.0, 0.60, 0.0, 0.70, 0.18, 0.80, 0.15, 0.88, 0.0, 1.0, 0.0),
    footR_yaw=k(-1.0, 0.0, -0.70, 10.0, -0.55, 45.0, -0.47, 60.0, -0.17, 60.0, 0.0, 30.0, 0.20, 0.0, 1.0, 0.0),
    footR_heel=k(-1.0, 55.0, -0.97, 60.0, -0.90, 70.0, -0.80, 60.0, -0.70, 30.0, -0.58, 10.0, -0.50, 0.0, -0.47, 0.0, -0.32, 5.0, -0.24, 35.0, -0.17, 60.0, -0.05, 75.0, 0.10, 60.0, 0.20, 30.0, 0.30, 5.0, 0.36, 0.0, 0.50, 25.0, 0.60, 55.0, 0.70, 40.0, 0.80, 10.0, 0.88, 0.0, 1.0, 5.0),
    footL_f=k(-1.0, -4.00, -0.90, -3.30, -0.84, -2.92, -0.70, -2.92, -0.62, -2.55, -0.50, -1.85, -0.40, -1.15, -0.30, -0.50, -0.22, -0.05, -0.17, 0.20, 0.30, 0.20, 0.40, 0.75, 0.50, 1.35, 0.58, 1.85, 0.62, 2.00, 0.85, 2.00, 0.95, 2.35, 1.0, 2.50),
    footL_r=k(-1.0, -0.10, -0.70, -0.10, -0.17, -0.05, 0.30, -0.05, 0.62, -0.20, 0.85, -0.20, 1.0, -0.30),
    footL_lift=k(-1.0, 0.22, -0.92, 0.12, -0.86, 0.02, -0.84, 0.0, -0.70, 0.0, -0.62, 0.30, -0.55, 0.50, -0.45, 0.50, -0.35, 0.38, -0.25, 0.15, -0.20, 0.04, -0.17, 0.0, 0.30, 0.0, 0.40, 0.20, 0.50, 0.22, 0.58, 0.05, 0.62, 0.0, 0.85, 0.0, 0.93, 0.15, 1.0, 0.18),
    footL_yaw=k(-1.0, 0.0, -0.30, 10.0, -0.17, 15.0, 0.30, 10.0, 0.62, 0.0),
    footL_heel=k(-1.0, 40.0, -0.86, 10.0, -0.84, 0.0, -0.76, 20.0, -0.70, 50.0, -0.62, 60.0, -0.50, 30.0, -0.30, 10.0, -0.17, 0.0, 0.18, 0.0, 0.30, 45.0, 0.40, 60.0, 0.50, 20.0, 0.62, 0.0, 0.85, 20.0, 0.93, 50.0, 1.0, 40.0),
    # The bowling arm: the ball carried by the right ear on the way in (elbow down and out), up by the face through the bound, then one
    # unbroken circle: down past the hip, back, straight over the top to release just past vertical, and down
    # across the body to the left knee. The elbow stays straight from the arm's horizontal to the release.
    armR_a=k(-1.0, 135.0, -0.70, 130.0, -0.58, 120.0, -0.47, 125.0, -0.38, 95.0, -0.28, 30.0, -0.18, -35.0, -0.10, -110.0, -0.04, -165.0, 0.0, -190.0, 0.05, -235.0, 0.10, -285.0, 0.18, -335.0, 0.30, -360.0, 0.50, -370.0, 0.70, -335.0, 0.85, -385.0, 1.0, -340.0),
    armR_p=k(-1.0, -10.0, -0.58, -25.0, -0.38, -5.0, -0.18, 15.0, 0.0, 8.0, 0.10, -15.0, 0.18, -45.0, 0.30, -55.0, 0.60, -10.0, 0.80, 0.0),
    armR_reach=k(-1.0, 0.35, -0.70, 0.35, -0.58, 0.40, -0.47, 0.42, -0.38, 0.75, -0.28, 0.97, -0.18, 1.0, 0.10, 1.0, 0.30, 0.95, 0.50, 0.85, 0.70, 0.75, 1.0, 0.72),
    armR_pole_f=k(-1.0, -0.2, -0.58, 0.0, -0.38, -0.5, -0.18, 0.0, 0.0, 0.0, 0.30, 0.3, 0.70, -1.0),
    armR_pole_r=k(-1.0, 0.5, -0.58, 0.8, -0.38, 1.0, 0.0, 1.0, 0.30, 0.8, 0.70, 0.15),
    armR_pole_z=k(-1.0, -1.0, -0.58, -1.0, -0.38, 0.0, 0.0, 0.0, 0.30, 0.3, 0.70, -0.4),
    # The palm under the ball on the way in, beside the cheek in the bound, turned away from the batter as the arm
    # goes down and back, facing the batter at release with the wrist cocked, then flicked down through the ball.
    handR_palm_f=k(-1.0, 0.2, -0.58, 0.5, -0.38, -0.3, -0.18, -0.6, -0.08, -0.3, 0.0, 1.0, 0.06, 0.4, 0.18, -0.5,
                   0.50, 0.0),
    handR_palm_r=k(-1.0, -0.6, -0.58, -0.7, -0.38, -0.8, -0.18, -0.6, -0.08, -0.2, 0.0, -0.2, 0.06, -0.5,
                   0.18, -0.5, 0.50, -1.0),
    handR_palm_z=k(-1.0, 0.6, -0.58, 0.2, -0.38, 0.0, -0.18, 0.2, -0.08, 1.0, 0.0, 0.0, 0.06, -0.8, 0.18, -0.2,
                   0.50, 0.0),
    handR_flex=k(-1.0, -20.0, -0.58, -35.0, -0.18, -10.0, -0.06, -45.0, 0.0, -30.0, 0.06, 40.0, 0.15, 15.0,
                 0.50, 0.0),
    # The front arm: pumping on the way in, up past the face in the bound to point straight up at the batter
    # through the coil, then pulled hard down and out behind, nearly straight, as the bowling arm comes over. Behind
    # the body (a negative angle) a negative armX_p swings the plane out to the arm's side; a positive one folds
    # the hand back onto the hip.
    armL_a=k(-1.0, -30.0, -0.84, 40.0, -0.70, -5.0, -0.58, 80.0, -0.47, 125.0, -0.35, 160.0, -0.20, 165.0,
             -0.08, 110.0, 0.0, 40.0, 0.06, -20.0, 0.15, -50.0, 0.30, -40.0, 0.50, 20.0, 0.70, -30.0, 0.90, 30.0,
             1.0, 0.0),
    armL_p=k(-1.0, 0.0, -0.58, -30.0, -0.47, -10.0, -0.35, 10.0, 0.0, 40.0, 0.04, 0.0, 0.10, -45.0, 0.50, -40.0, 0.75, 0.0),
    armL_reach=k(-1.0, 0.75, -0.58, 0.60, -0.47, 0.80, -0.35, 0.98, -0.20, 1.0, -0.08, 0.80, 0.0, 0.70,
                 0.15, 0.92, 0.50, 0.90, 0.80, 0.75),
    armL_pole_f=k(-1.0, -1.0, -0.70, -1.0, -0.47, 0.0, -0.20, 0.0, 0.0, -0.6, 0.20, -0.5, 0.60, -1.0),
    armL_pole_r=k(-1.0, -0.15, -0.70, -0.3, -0.47, -1.0, -0.20, -1.0, 0.0, -0.8, 0.20, -1.0, 0.60, -0.15),
    armL_pole_z=k(-1.0, -0.4, -0.70, -0.4, -0.47, -0.3, -0.20, 0.0, 0.0, -0.2, 0.20, 0.0, 0.60, -0.4),
    handL_palm_f=k(-1.0, 0.0, -0.35, 0.6, -0.08, 0.0, 0.15, -0.3, 0.50, 0.0),
    handL_palm_r=k(-1.0, 1.0, -0.35, 0.5, -0.08, 1.0, 0.15, 0.8, 0.50, 1.0),
    handL_palm_z=k(-1.0, 0.0, -0.35, 0.0),
    handL_flex=k(-1.0, 0.0, -0.35, -15.0, 0.0, 10.0, 0.50, 0.0),
)

# The off spinner walks in at about 3.3 m/s with the ball by the right ear, hops into a small bound keeping it
# there, elbow down, while the front arm goes up at the batter, then the same unbroken circle of the arm at a
# spinner's pace, the fingers turning over the ball at release, the trunk well over the front leg and the front
# arm out behind, and is walking upright again half a second later.
OFF_SPIN = dict(
    LOOK,
    hip_f=k(-1.0, -2.82, -0.88, -2.42, -0.74, -1.96, -0.47, -1.10, -0.20, -0.35, 0.0, 0.0, 0.15, 0.30, 0.35, 0.65, 0.60, 1.10, 1.0, 1.60),
    hip_r=k(-1.0, 0.0, -0.20, 0.0, 0.40, -0.05, 1.0, -0.30),
    hip_z=k(-1.0, -0.04, -0.87, -0.02, -0.80, -0.05, -0.74, -0.03, -0.60, 0.07, -0.47, -0.07, -0.20, -0.12,
            0.0, -0.09, 0.20, -0.15, 0.45, -0.07, 0.70, -0.03, 1.0, -0.02),
    hip_yaw=k(-1.0, 0.0, -0.74, 5.0, -0.60, 20.0, -0.47, 40.0, -0.20, 30.0, 0.0, 0.0, 0.20, -20.0, 0.45, -15.0,
              0.80, 0.0),
    hip_bend=k(-1.0, 8.0, -0.47, 5.0, 0.0, 12.0, 0.20, 30.0, 0.45, 20.0, 0.80, 6.0),
    hip_side=k(-1.0, 0.0),
    ch_yaw=k(-1.0, 0.0, -0.74, 10.0, -0.60, 40.0, -0.47, 60.0, -0.25, 62.0, -0.10, 35.0, 0.0, 5.0, 0.10, -25.0,
             0.25, -45.0, 0.45, -30.0, 0.70, -8.0, 1.0, 0.0),
    ch_bend=k(-1.0, 8.0, -0.60, 0.0, -0.40, -6.0, -0.20, -2.0, -0.05, 10.0, 0.0, 22.0, 0.10, 42.0, 0.22, 58.0,
              0.40, 48.0, 0.60, 25.0, 0.85, 8.0, 1.0, 5.0),
    ch_side=k(-1.0, 0.0, -0.47, 8.0, -0.25, 8.0, -0.10, 0.0, 0.0, -18.0, 0.12, -12.0, 0.35, 0.0),
    clavR_up=k(-1.0, 0.0, -0.47, 15.0, -0.37, 15.0, -0.25, 0.0, -0.10, 0.0, -0.02, 18.0, 0.10, 8.0, 0.25, 0.0),
    clavL_up=k(-1.0, 0.0, -0.47, 15.0, -0.30, 18.0, -0.10, 12.0, 0.05, 0.0),
    footR_f=k(-1.0, -2.95, -0.92, -2.95, -0.80, -2.40, -0.66, -1.70, -0.55, -1.15, -0.47, -0.95, -0.20, -0.95, -0.10, -0.75, 0.0, -0.45, 0.12, 0.10, 0.25, 0.70, 0.35, 0.95, 0.65, 0.95, 0.78, 1.40, 0.90, 1.75, 1.0, 1.75),
    footR_r=k(-1.0, 0.10, -0.47, 0.08, -0.20, 0.08, 0.35, 0.05, 0.65, 0.05, 0.90, -0.20, 1.0, -0.20),
    footR_lift=k(-1.0, 0.0, -0.92, 0.0, -0.85, 0.15, -0.75, 0.22, -0.62, 0.20, -0.52, 0.05, -0.47, 0.0, -0.20, 0.0, -0.10, 0.02, 0.0, 0.05, 0.12, 0.15, 0.25, 0.15, 0.33, 0.02, 0.35, 0.0, 0.65, 0.0, 0.75, 0.12, 0.85, 0.05, 0.90, 0.0, 1.0, 0.0),
    footR_yaw=k(-1.0, 0.0, -0.60, 30.0, -0.47, 55.0, -0.20, 50.0, -0.02, 30.0, 0.25, 0.0),
    footR_heel=k(-1.0, 30.0, -0.92, 55.0, -0.85, 50.0, -0.70, 15.0, -0.52, 0.0, -0.47, 0.0, -0.32, 10.0, -0.24, 40.0, -0.20, 55.0, -0.05, 70.0, 0.12, 50.0, 0.25, 10.0, 0.35, 0.0, 0.55, 30.0, 0.65, 50.0, 0.78, 10.0, 0.90, 0.0, 1.0, 5.0),
    footL_f=k(-1.0, -2.90, -0.94, -2.60, -0.88, -2.25, -0.74, -2.25, -0.64, -1.85, -0.52, -1.20, -0.40, -0.60, -0.28, -0.05, -0.20, 0.20, 0.30, 0.20, 0.42, 0.75, 0.55, 1.20, 0.60, 1.35, 1.0, 1.35),
    footL_r=k(-1.0, -0.10, -0.74, -0.10, -0.20, -0.05, 0.30, -0.05, 0.60, -0.12, 1.0, -0.12),
    footL_lift=k(-1.0, 0.12, -0.92, 0.05, -0.88, 0.0, -0.74, 0.0, -0.64, 0.25, -0.52, 0.35, -0.40, 0.28, -0.30, 0.10, -0.22, 0.02, -0.20, 0.0, 0.30, 0.0, 0.40, 0.14, 0.50, 0.12, 0.58, 0.02, 0.60, 0.0, 1.0, 0.0),
    footL_yaw=k(-1.0, 0.0, -0.30, 10.0, -0.20, 12.0, 0.45, 10.0, 0.72, 0.0),
    footL_heel=k(-1.0, 30.0, -0.90, 5.0, -0.88, 0.0, -0.80, 20.0, -0.74, 45.0, -0.66, 50.0, -0.40, 20.0, -0.20, 0.0, 0.18, 0.0, 0.30, 45.0, 0.40, 50.0, 0.55, 10.0, 0.60, 0.0, 1.0, 20.0),
    # The ball by the right ear on the way in and through the hop, the elbow down and out; the bowling hand then
    # drops away in front, circles down past the hip and back up over the top.
    armR_a=k(-1.0, 130.0, -0.74, 128.0, -0.60, 132.0, -0.47, 135.0, -0.37, 132.0, -0.30, 110.0, -0.22, 45.0, -0.15, -25.0, -0.08, -110.0, -0.03, -160.0, 0.0, -185.0, 0.06, -230.0, 0.14, -290.0, 0.25, -340.0, 0.40, -365.0, 0.65, -350.0, 0.85, -370.0, 1.0, -355.0),
    armR_p=k(-1.0, -10.0, -0.60, -25.0, -0.47, -20.0, -0.30, -5.0, -0.15, 15.0, 0.0, 5.0, 0.14, -25.0, 0.25, -45.0, 0.40, -45.0, 0.70, -10.0),
    armR_reach=k(-1.0, 0.35, -0.74, 0.35, -0.60, 0.40, -0.47, 0.45, -0.37, 0.48, -0.30, 0.70, -0.22, 0.97, -0.12, 1.0, 0.14, 1.0, 0.40, 0.90, 0.65, 0.85, 1.0, 0.85),
    armR_pole_f=k(-1.0, -0.2, -0.60, -0.3, -0.47, 0.0, -0.30, -0.5, -0.12, 0.0, 0.25, 0.3, 0.65, -1.0),
    armR_pole_r=k(-1.0, 0.5, -0.60, 0.7, -0.37, 0.7, -0.20, 1.0, 0.25, 0.8, 0.65, 0.15),
    armR_pole_z=k(-1.0, -1.0, -0.60, -1.0, -0.37, -0.6, -0.20, 0.0, 0.25, 0.3, 0.65, -0.4),
    # At release the palm faces the leg side with the wrist cocked, and the fingers turn clockwise over the ball:
    # the palm ends up facing the sky, then the batter's off side, as the hand comes down.
    handR_palm_f=k(-1.0, 0.0, -0.47, 0.6, -0.30, 0.0, -0.15, -0.6, -0.06, -0.3, 0.0, 0.6, 0.06, 0.0, 0.14, -0.3,
                   0.50, 0.0),
    handR_palm_r=k(-1.0, -0.8, -0.47, -0.6, -0.30, -1.0, -0.15, -0.6, -0.06, -0.2, 0.0, -0.8, 0.06, -0.3,
                   0.14, 0.4, 0.50, -1.0),
    handR_palm_z=k(-1.0, 0.5, -0.47, 0.3, -0.30, 0.0, -0.15, 0.2, -0.06, 1.0, 0.0, 0.0, 0.06, 0.9, 0.14, 0.2,
                   0.50, 0.0),
    handR_flex=k(-1.0, 0.0, -0.47, -25.0, -0.12, -10.0, -0.04, -40.0, 0.0, -25.0, 0.08, 30.0, 0.20, 10.0,
                 0.50, 0.0),
    armL_a=k(-1.0, -20.0, -0.88, 25.0, -0.74, 40.0, -0.60, 70.0, -0.47, 110.0, -0.37, 155.0, -0.25, 165.0, -0.12, 120.0, 0.0, 45.0, 0.08, -20.0, 0.20, -50.0, 0.40, -45.0, 0.65, -10.0, 0.85, -15.0, 1.0, 0.0),
    armL_p=k(-1.0, 0.0, -0.74, -15.0, -0.60, -15.0, -0.47, -10.0, -0.30, 0.0, 0.0, 35.0, 0.05, 0.0, 0.12, -45.0, 0.45, -45.0, 0.70, -15.0, 0.85, 0.0),
    armL_reach=k(-1.0, 0.80, -0.74, 0.70, -0.60, 0.45, -0.47, 0.70, -0.37, 0.85, -0.25, 0.95, -0.12, 0.85, 0.0, 0.70, 0.15, 0.92, 0.50, 0.90, 0.80, 0.85),
    armL_pole_f=k(-1.0, -1.0, -0.74, -0.6, -0.37, 0.0, 0.0, -0.6, 0.20, -0.5, 0.70, -1.0),
    armL_pole_r=k(-1.0, -0.15, -0.74, -0.5, -0.37, -1.0, 0.0, -0.8, 0.20, -1.0, 0.70, -0.15),
    armL_pole_z=k(-1.0, -0.4, -0.74, -0.8, -0.37, 0.0, 0.0, -0.2, 0.20, 0.0, 0.70, -0.4),
    # The front hand swings on the way in, goes up at the batter through the hop and is pulled down and out behind.
    handL_palm_f=k(-1.0, 0.0, -0.47, 0.4, -0.30, 0.6, -0.08, 0.0, 0.20, -0.3, 0.60, 0.0),
    handL_palm_r=k(-1.0, 1.0, -0.47, 0.8, -0.30, 0.5, -0.08, 1.0, 0.20, 0.8, 0.60, 1.0),
    handL_palm_z=k(-1.0, 0.2, -0.47, 0.0),
    handL_flex=k(-1.0, 0.0, -0.37, -15.0, 0.0, 10.0, 0.60, 0.0),
)

# The wrist spinner: the off spinner's approach, bound and circle (the broadcast has no leg spinner to key from),
# with the leg break's hand: the wrist cocked in and the back of the hand to the batter at release, flicked
# anticlockwise so the palm ends facing the bowler's own chest.
LEG_SPIN = dict(OFF_SPIN,
                ch_yaw=k(-1.0, 0.0, -0.74, 10.0, -0.60, 30.0, -0.47, 45.0, -0.25, 48.0, -0.10, 25.0, 0.0, 0.0,
                         0.10, -25.0, 0.25, -45.0, 0.45, -30.0, 0.70, -8.0, 1.0, 0.0),
                handR_palm_f=k(-1.0, 0.0, -0.47, 0.6, -0.30, 0.0, -0.15, -0.6, -0.06, -0.5, 0.0, -0.3, 0.06, -0.6,
                               0.14, -0.3, 0.50, 0.0),
                handR_palm_r=k(-1.0, -0.8, -0.47, -0.6, -0.30, -1.0, -0.15, -0.6, -0.06, -0.8, 0.0, -0.9, 0.06, -0.6,
                               0.14, -0.9, 0.50, -1.0),
                handR_palm_z=k(-1.0, 0.5, -0.47, 0.3, -0.30, 0.0, -0.15, 0.2, -0.06, 0.4, 0.0, 0.2, 0.06, -0.3,
                               0.14, 0.0, 0.50, 0.0),
                handR_flex=k(-1.0, 0.0, -0.47, -25.0, -0.12, 10.0, -0.04, 40.0, 0.0, 45.0, 0.08, 20.0, 0.20, 5.0,
                             0.50, 0.0))

KEYS = {'Pace': PACE, 'OffSpin': OFF_SPIN, 'LegSpin': LEG_SPIN}


def mirror(keys):
    """The left-armer's action: every lateral place, turn and lean flips, and the two sides swap."""
    out = {}
    for name, track in keys.items():
        flip = name.endswith('_r') or 'yaw' in name or 'side' in name
        side = next((s for s in ('foot', 'arm', 'hand', 'clav') if name.startswith(s)), None)
        if side:
            x = name[len(side)]
            name = side + {'L': 'R', 'R': 'L'}[x] + name[len(side) + 1:]
        out[name] = [(t, -v if flip else v) for t, v in track]
    return out
