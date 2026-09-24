# Unreal side of Scripts/stadium/make_stadium.sh: imports the printed textures and builds the stadium's materials
# in /Game/Stadium. Every surface is drawn in HLSL from its world position (metres from the pitch centre, x along the
# pitch), so the ground needs no UVs and no photographic textures: grass, pitch and LED boards are all procedural.
import os
import unreal

DIR = os.environ["STADIUM_DIR"]
DEST = "/Game/Stadium"
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

# Textures are read with Texture2DSample, not .Sample: the ray tracing hit shaders cannot take implicit derivatives,
# and the macro turns into SampleLevel there.
# Shared noise, as methods of a struct because a Custom node's code is one function body. The hash has no sine,
# so it stays stable far from the origin. S is smoothstep that also runs downhill (a > b): Metal leaves the
# built-in undefined there, and on a Mac it came out as noise.
STEP = """
    float S(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
"""
NOISE = """
struct FNoise
{""" + STEP + """
    float H(float2 p) { float3 p3 = frac(float3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return frac((p3.x + p3.y) * p3.z); }
    float V(float2 p)
    {
        float2 i = floor(p), f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(H(i), H(i + float2(1, 0)), f.x), lerp(H(i + float2(0, 1)), H(i + float2(1, 1)), f.x), f.y);
    }
    float F(float2 p)
    {
        float s = 0, a = 0.5;
        for (int k = 0; k < 4; k++) { s += a * V(p); p = p * 2.03 + 17.1; a *= 0.5; }
        return s / 0.9375;
    }
};
FNoise n;
float2 q = (P.xy - float2(Cx, 0)) * 0.01;
float px = max(length(ddx(q)), length(ddy(q))); // metres per pixel: fine detail fades out below it
"""

GRASS = NOISE + """
float3 c = float3(0.1, 0.19, 0.05);
// Mowing stripes: 5 m bands square to the pitch, the light and dark of grass laid away from and toward the eye.
c *= lerp(0.9, 1.1, n.S(-0.15, 0.15, sin(q.x * 3.14159 / 5.0)));
// Patches of lusher and drier grass.
float m = n.F(q / 11.0), y = n.F(q / 29.0 + 5.3);
c *= 0.88 + 0.24 * m;
c = lerp(c, c * float3(1.35, 1.05, 0.7), saturate(y * 1.6 - 0.5) * 0.5);
// Blades: two octaves of fine noise.
float fine = (n.V(q * 23.0) - 0.5) * 0.5 + (n.V(q * 71.0) - 0.5) * 0.5;
c *= 1.0 + fine * 0.25 * saturate(1.0 - px * 25.0);
// The square: the block of prepared pitches, mown shorter and paler, each old strip a slightly different shade.
float sq = n.S(0.6, 0.0, max(abs(q.x) - 14.0, abs(q.y) - 10.5));
float strips = n.H(float2(floor(q.y / 3.05 + 0.5), 7.0));
c = lerp(c, c * float3(1.25, 1.12, 0.9) * (0.94 + 0.12 * strips), sq);
// Wear: bare earth in patches on the bowlers' run-ups and round the batters' ground.
float ax = abs(q.x), ay = abs(q.y);
float run = n.S(1.8, 0.7, ay) * n.S(27.0, 14.0, ax) * n.S(10.8, 11.6, ax);
float crease = n.S(2.6, 1.6, ay) * n.S(1.4, 0.3, abs(ax - 10.0));
float wear = saturate(max(run * 0.8, crease * 0.6) * n.S(0.35, 0.65, n.F(q * 2.3)));
c = lerp(c, float3(0.28, 0.2, 0.11) * (0.8 + 0.4 * n.V(q * 9.0)), wear);
// The 30-yard circle: a ring of painted dots.
float d = length(q), s = atan2(q.y, q.x) * 27.4;
float dotm = n.S(0.26, 0.2, length(float2((frac(s / 2.4) - 0.5) * 2.4, d - 27.4)));
// Painted logos beyond each set of stumps, reading the right way from the main camera.
float paint = dotm;
for (int e = 0; e < 2; e++)
{
    float2 uv = float2((-q.y + 5.5) / 11.0, (q.x - (e == 0 ? -19.0 : 19.0)) / 2.75 + 0.5);
    float inside = all(uv == saturate(uv)) ? 1.0 : 0.0;
    paint = max(paint, Texture2DSample(Logo, LogoSampler, saturate(uv)).a * inside * 0.4);
}
c = lerp(c, float3(0.62, 0.64, 0.6), paint);
Rough = 0.85 + 0.1 * wear;
return c;
"""

PITCH = NOISE + """
float ax = abs(q.x), ay = abs(q.y);
// The venue's pitch (game parameters): Green is the grass left on it, Dust how dry and powdery it has gone, Wear how
// much has been played on it, RoughSpots how torn the footmarks outside the stumps are.
float3 c = float3(0.47, 0.37, 0.2);
// Clay: lighter where rolled, darker where damp, dusty in drifts; a dry pitch goes pale and powdery.
float m = n.F(q * float2(0.9, 2.2));
c *= 0.84 + 0.32 * m;
c = lerp(c, float3(0.56, 0.47, 0.3), n.S(0.4, 0.8, n.F(q * 1.7 + 9.1)) * (0.5 + 0.4 * Dust));
c = lerp(c, float3(0.6, 0.5, 0.34) * (0.9 + 0.2 * n.F(q * 3.1 + 4.4)), 0.35 * Dust);
// Grass: live grass left on the pitch, most of it towards the edges; a green top is covered all over.
float gn = n.F(q * 5.0 + 3.7);
float g = lerp(saturate(0.35 + 0.65 * n.S(0.7, 1.45, ay)) * n.S(0.45, 0.75, gn), 0.75 + 0.25 * gn, Green);
float blade = 0.8 + 0.4 * n.V(q * float2(60.0, 140.0));
c = lerp(c, float3(0.2, 0.25, 0.09) * blade, saturate(g * (0.55 + 0.35 * Green) * (1.0 - 0.5 * Dust)));
// Cracks: the edges of irregular cells, only where the surface has dried out.
float2 cp = q / 0.32, ci = floor(cp);
float f1 = 9.0, f2 = 9.0;
for (int j = -1; j <= 1; j++)
    for (int i = -1; i <= 1; i++)
    {
        float2 o = float2(i, j);
        float2 r = o + float2(n.H(ci + o), n.H(ci + o + 19.7)) - frac(cp);
        float dd = dot(r, r);
        if (dd < f1) { f2 = f1; f1 = dd; } else if (dd < f2) f2 = dd;
    }
float dry = saturate(0.7 - 0.6 * Green + 0.5 * Dust);
float crack = (1.0 - n.S(0.0, 0.06 + 0.03 * Dust, sqrt(f2) - sqrt(f1))) * n.S(0.9 - 0.5 * dry, 1.1 - 0.5 * dry, n.F(q * 0.8 + 2.0)) * saturate(1.0 - px * 60.0);
c *= 1.0 - 0.55 * crack;
// Worn ends round each popping crease: the batters' guard and the bowlers' landing, scuffed and footmarked.
float end = n.S(2.6, 0.4, abs(ax - 8.84));
// Footmarks: scattered, each at a random spot and angle in its cell, thickest where the feet land, in the middle.
float2 fp = q / float2(0.3, 0.22), fi = floor(fp);
float2 ff = frac(fp) - 0.5 - (float2(n.H(fi + 5.1), n.H(fi + 8.7)) - 0.5) * 0.5;
float fa = n.H(fi + 1.9) * 3.14159, fs = sin(fa), fc = cos(fa);
float2 fr = float2(ff.x * fc - ff.y * fs, ff.x * fs + ff.y * fc);
float land = n.S(1.4, 0.2, abs(ax - 8.84)) * n.S(1.1, 0.4, ay);
float foot = step(n.H(fi + 3.3), (0.15 + 0.3 * Wear) * land) * n.S(0.3, 0.12, length(fr * float2(1.0, 1.8)));
c = lerp(c, c * float3(0.8, 0.76, 0.7), end * (0.2 + 0.4 * Wear));
c = lerp(c, float3(0.3, 0.23, 0.13), 0.35 * foot);
// The rough outside the stumps on a spinner's length at the batter's end (CricketBall::IsInRough): dug-up, dusty
// patches of scattered footmarks.
float rough = n.S(0.25, 0.4, ay) * n.S(1.2, 1.0, ay) * n.S(-8.0, -7.7, q.x) * n.S(-4.9, -5.2, q.x) * RoughSpots;
float scuff = n.S(0.35, 0.65, n.F(q * 7.0 + 12.3));
c = lerp(c, float3(0.5, 0.4, 0.26) * (0.75 + 0.5 * n.V(q * 40.0)), rough * scuff * 0.8);
// Ball marks on a good length at the striker's end.
float2 bp = q / 0.18;
float bm = step(0.93, n.H(floor(bp) + 41.0)) * n.S(0.3, 0.15, length(frac(bp) - 0.5)) * n.S(8.0, 3.0, abs(q.x + 3.0)) * n.S(0.9, 0.3, ay);
c *= 1.0 - 0.3 * bm * Wear;
// Marks the game draws where this match's balls pitched and the bowlers' feet landed: a scuffed, darker spot.
float2 mu = float2(q.x / 23.0 + 0.5, q.y / 3.2 + 0.5);
float mk = saturate(Texture2DSample(Marks, MarksSampler, mu).r * 1.5) * MarksOn;
c = lerp(c, c * float3(0.55, 0.5, 0.45), mk);
c *= 1.0 + (n.V(q * 90.0) - 0.5) * 0.18 * saturate(1.0 - px * 80.0);
Rough = 0.78 + 0.15 * m + 0.1 * Dust - 0.1 * Green;
return c;
"""

# LED boards (and any other LED band): the arc round the ground picks an 8 m panel, the height within the band
# (vertex colour red, 0 at the bottom) the row of the panel's ad. Two ads alternate round the ground and all the
# panels change together every 7 s, as a real LED ribbon does. The LED grid shows only close up.
LED = """
struct FStep
{""" + STEP + """};
FStep n;
float2 q = (P.xy - float2(Cx, 0)) * 0.01;
float s = (atan2(q.y, q.x) + 3.14159) * length(q);
float panel = floor(s / 8.0);
float row = fmod(floor(T / 7.0) + fmod(panel, 2.0) * 4.0 + 64.0, 8.0);
float2 uv = float2(frac(s / 8.0), (row + saturate(1.0 - V)) / 8.0);
float3 c = Texture2DSample(Ads, AdsSampler, uv).rgb;
float2 cell = frac(float2(uv.x * 1024.0, uv.y * 1024.0)) - 0.5;
float px = max(length(ddx(uv * 1024.0)), length(ddy(uv * 1024.0)));
c *= lerp(1.0, n.S(0.5, 0.25, max(abs(cell.x), abs(cell.y))) * 1.4, saturate(1.0 - px));
return c;
"""

# The crowd. The fan (make_fan.py) carries its region in vertex colour and its two cheering poses in UV0-2; each
# instance carries its shirt colour (custom data 0-2), skin tone (3) and a phase (4) that sets its rhythm, its pose
# and its hair and trousers. Excite (0 to 1) comes from the game.
CROWD_MOVE = """
float3 a = float3(U0.x, 1.0 - U0.y, U1.x), b = float3(1.0 - U1.y, U2.x, 1.0 - U2.y); // the importer flipped each V
float rate = 1.4 + frac(Ph * 13.7) * 1.2;
float beat = 0.5 + 0.5 * sin(6.28318 * (rate * T + Ph));
// Half the fans get up with both arms high and bounce; the rest stay seated and pump one arm. A few wave anyway.
float standing = step(frac(Ph * 7.31), 0.5);
float up = saturate(E * 1.3) * lerp(0.7, 1.0, beat);
float idle = step(0.96, frac(Ph * 31.7)) * beat * 0.5;
return lerp(b * max(E * beat, idle), a * up + b * idle * (1.0 - up), standing);
"""

# A flag held up by a seated fan (CricketStadium::Flag): the whole flag swings from the hand, side to side, and the
# cloth ripples toward its fly end; both grow with the crowd's excitement. LP is the vertex's local position (cm).
FLAG_MOVE = """
float rate = 0.7 + frac(Ph * 13.7) * 0.6 + 0.8 * E;
float a = (0.25 + 0.45 * E) * sin(6.28318 * (rate * T + Ph));
float2 d = LP.yz, r = float2(d.x * cos(a) - d.y * sin(a), d.x * sin(a) + d.y * cos(a));
float ripple = VC.r * (5.0 + 6.0 * E) * sin(LP.y * 0.09 - T * (6.0 + 5.0 * E) + Ph * 40.0);
return float3(ripple, r - d);
"""

# The cloth in the team colour with a white band across the middle; the pole grey.
FLAG_LOOK = """
float3 c = lerp(Col, float3(0.8, 0.8, 0.78), step(abs(VC.b - 0.5), 0.14));
return lerp(float3(0.35, 0.35, 0.37), c, VC.g);
"""

CROWD_LOOK = """
float3 skin = lerp(float3(0.16, 0.09, 0.055), float3(0.66, 0.46, 0.34), sqrt(Skin));
float h = frac(Ph * 5.3), t = frac(Ph * 3.7);
float3 hair = h < 0.7 ? float3(0.015, 0.012, 0.01) : h < 0.85 ? float3(0.08, 0.05, 0.03) : h < 0.95 ? float3(0.25, 0.25, 0.25) : float3(0.45, 0.32, 0.15);
float3 legs = t < 0.5 ? float3(0.03, 0.04, 0.07) : t < 0.8 ? float3(0.02, 0.02, 0.02) : float3(0.3, 0.26, 0.18);
float3 c = legs;
c = lerp(c, Shirt, VC.r);
c = lerp(c, skin, VC.g);
c = lerp(c, hair, VC.b);
return c;
"""


def import_texture(name):
    t = unreal.AssetImportTask()
    t.filename = os.path.join(DIR, f"{name}.png")
    t.destination_path = DEST
    t.destination_name = name
    t.automated = True
    t.replace_existing = True
    t.save = True
    tools.import_asset_tasks([t])
    return unreal.load_asset(f"{DEST}/{name}")


def new_material(name):
    if lib.does_asset_exist(f"{DEST}/{name}"):
        lib.delete_asset(f"{DEST}/{name}")
    return tools.create_asset(name, DEST, unreal.Material, unreal.MaterialFactoryNew())


def custom(m, code, inputs, extra=()):
    c = mel.create_material_expression(m, unreal.MaterialExpressionCustom, -400, 0)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for name in inputs:
        i = unreal.CustomInput()
        i.set_editor_property("input_name", name)
        ins.append(i)
    c.set_editor_property("inputs", ins)
    outs = []
    for name in extra:
        o = unreal.CustomOutput()
        o.set_editor_property("output_name", name)
        o.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        outs.append(o)
    c.set_editor_property("additional_outputs", outs)
    return c


def world_inputs(m, c):
    pos = mel.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -800, 0)
    mel.connect_material_expressions(pos, "", c, "P")
    cx = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 100)
    cx.set_editor_property("parameter_name", "PitchCentreX")
    cx.set_editor_property("default_value", 1006.0)
    mel.connect_material_expressions(cx, "", c, "Cx")


def constant(m, value, prop, y):
    k = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -400, y)
    k.set_editor_property("r", value)
    mel.connect_material_property(k, "", prop)


def texture_input(m, c, name, texture):
    t = mel.create_material_expression(m, unreal.MaterialExpressionTextureObject, -800, 200)
    t.set_editor_property("texture", texture)
    mel.connect_material_expressions(t, "", c, name)


def finish(m):
    mel.recompile_material(m)
    lib.save_loaded_asset(m)
    unreal.log(f"STADIUM built {m.get_path_name()}")


def ground(name, code, logo=None, params=(), marks=None):
    m = new_material(name)
    c = custom(m, code, ["P", "Cx"] + (["Logo"] if logo else []) + [k for k, _ in params] + (["Marks", "MarksOn"] if marks else []), ["Rough"])
    world_inputs(m, c)
    if logo:
        texture_input(m, c, "Logo", logo)
    for i, (k, v) in enumerate(list(params) + ([("MarksOn", 0.0)] if marks else [])):
        e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 300 + 60 * i)
        e.set_editor_property("parameter_name", k)
        e.set_editor_property("default_value", v)
        mel.connect_material_expressions(e, "", c, k)
    if marks:
        t = mel.create_material_expression(m, unreal.MaterialExpressionTextureObjectParameter, -800, 700)
        t.set_editor_property("parameter_name", "Marks")
        t.set_editor_property("texture", marks)
        mel.connect_material_expressions(t, "", c, "Marks")
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(c, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    constant(m, 0.3, unreal.MaterialProperty.MP_SPECULAR, 300)
    finish(m)


def led(ads):
    m = new_material("M_LED")
    c = custom(m, LED, ["P", "Cx", "T", "V", "Ads"])
    world_inputs(m, c)
    t = mel.create_material_expression(m, unreal.MaterialExpressionTime, -800, 300)
    mel.connect_material_expressions(t, "", c, "T")
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -800, 400)
    mel.connect_material_expressions(vc, "R", c, "V")
    texture_input(m, c, "Ads", ads)
    # Lit like a painted board and glowing on top of that, so it reads in the sun and in the stand's shade.
    glow = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 200)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 4000.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 200)
    mel.connect_material_expressions(c, "", mul, "A")
    mel.connect_material_expressions(glow, "", mul, "B")
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    constant(m, 0.35, unreal.MaterialProperty.MP_ROUGHNESS, 400)
    finish(m)


# The big screen: whatever the game draws into its Screen texture. (No LED pixel grid: at stadium distances it
# only shimmers into moire.)
SCREEN = """
return Texture2DSample(Screen, ScreenSampler, UV).rgb;
"""


def screen(ads):
    m = new_material("M_Screen")
    c = custom(m, SCREEN, ["UV", "Screen"])
    uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -800, 0)
    mel.connect_material_expressions(uv, "", c, "UV")
    t = mel.create_material_expression(m, unreal.MaterialExpressionTextureObjectParameter, -800, 200)
    t.set_editor_property("parameter_name", "Screen")
    t.set_editor_property("texture", ads)
    mel.connect_material_expressions(t, "", c, "Screen")
    glow = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 200)
    glow.set_editor_property("parameter_name", "Glow")
    glow.set_editor_property("default_value", 6000.0)
    mul = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 200)
    mel.connect_material_expressions(c, "", mul, "A")
    mel.connect_material_expressions(glow, "", mul, "B")
    mel.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    constant(m, 0.0, unreal.MaterialProperty.MP_BASE_COLOR, 300)
    constant(m, 0.3, unreal.MaterialProperty.MP_ROUGHNESS, 400)
    finish(m)


def import_fan():
    base = os.path.join(DIR, "Fan_LOD0.fbx")
    if not os.path.exists(base):
        unreal.log("STADIUM no Fan_LOD0.fbx: the game keeps its block crowd")
        return
    t = unreal.AssetImportTask()
    t.filename = base
    t.destination_path = DEST
    t.destination_name = "SM_Fan"
    t.automated = True
    t.replace_existing = True
    t.save = True
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("generate_lightmap_u_vs", False)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("vertex_color_import_option", unreal.VertexColorImportOption.REPLACE)
    t.options = ui
    tools.import_asset_tasks([t])
    mesh = unreal.load_asset(f"{DEST}/SM_Fan")
    # The editor subsystem is not there in a commandlet; its class default object does the same work.
    sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
    for lod in (1, 2):
        sub.import_lod(mesh, lod, os.path.join(DIR, f"Fan_LOD{lod}.fbx"))
    # Full precision: the poses are centimetres in the UVs, and half floats would round them to millimetres at best.
    for lod in range(3):
        b = sub.get_lod_build_settings(mesh, lod)
        b.set_editor_property("use_full_precision_u_vs", True)
        b.set_editor_property("generate_lightmap_u_vs", False)
        sub.set_lod_build_settings(mesh, lod, b)
    # A seated fan fills 5% of the screen height in a close shot of the stand; the main view sees them far smaller.
    sub.set_lod_screen_sizes(mesh, [1.0, 0.05, 0.015])
    lib.save_loaded_asset(mesh)
    unreal.log(f"STADIUM built {mesh.get_path_name()} with {sub.get_lod_count(mesh)} LODs")


def fill(m, look):
    # Emissive fill, Fill times the base colour: the light that reaches a packed stand under its roof from the field and
    # the sky, which the renderer's own bounce loses among thousands of spectators shading each other.
    k = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 200)
    k.set_editor_property("parameter_name", "Fill")
    x = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -200, 200)
    mel.connect_material_expressions(look, "", x, "A")
    mel.connect_material_expressions(k, "", x, "B")
    mel.connect_material_property(x, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)


def per_instance(m, index, y, vector=False):
    e = mel.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData3Vector if vector else unreal.MaterialExpressionPerInstanceCustomData, -800, y)
    e.set_editor_property("data_index", index)
    return e


def crowd():
    m = new_material("M_Crowd")
    m.set_editor_property("used_with_instanced_static_meshes", True)
    look = custom(m, CROWD_LOOK, ["VC", "Shirt", "Skin", "Ph"])
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -800, -200)
    mel.connect_material_expressions(vc, "", look, "VC")
    mel.connect_material_expressions(per_instance(m, 0, -100, True), "", look, "Shirt")
    skin = per_instance(m, 3, 0)
    phase = per_instance(m, 4, 100)
    mel.connect_material_expressions(skin, "", look, "Skin")
    mel.connect_material_expressions(phase, "", look, "Ph")
    mel.connect_material_property(look, "", unreal.MaterialProperty.MP_BASE_COLOR)
    fill(m, look)
    constant(m, 0.8, unreal.MaterialProperty.MP_ROUGHNESS, 300)

    move = custom(m, CROWD_MOVE, ["U0", "U1", "U2", "Ph", "E", "T"])
    move.set_editor_property("material_expression_editor_y", 500)
    for k in range(3):
        uv = mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -800, 400 + 60 * k)
        uv.set_editor_property("coordinate_index", k)
        mel.connect_material_expressions(uv, "", move, f"U{k}")
    mel.connect_material_expressions(phase, "", move, "Ph")
    excite = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 600)
    excite.set_editor_property("parameter_name", "Excite")
    mel.connect_material_expressions(excite, "", move, "E")
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -800, 660)
    mel.connect_material_expressions(time, "", move, "T")
    # The poses are in the fan's own space: turn and scale them with the instance.
    world = mel.create_material_expression(m, unreal.MaterialExpressionTransform, -200, 500)
    world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    mel.connect_material_expressions(move, "", world, "")
    mel.connect_material_property(world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish(m)


def flag():
    m = new_material("M_Flag")
    m.set_editor_property("used_with_instanced_static_meshes", True)
    m.set_editor_property("two_sided", True)
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -800, -200)
    phase = per_instance(m, 3, 100)
    look = custom(m, FLAG_LOOK, ["VC", "Col"])
    mel.connect_material_expressions(vc, "", look, "VC")
    mel.connect_material_expressions(per_instance(m, 0, -100, True), "", look, "Col")
    mel.connect_material_property(look, "", unreal.MaterialProperty.MP_BASE_COLOR)
    fill(m, look)
    constant(m, 0.7, unreal.MaterialProperty.MP_ROUGHNESS, 300)

    move = custom(m, FLAG_MOVE, ["LP", "VC", "Ph", "E", "T"])
    move.set_editor_property("material_expression_editor_y", 500)
    lp = mel.create_material_expression(m, unreal.MaterialExpressionLocalPosition, -800, 400)
    mel.connect_material_expressions(lp, "", move, "LP")
    mel.connect_material_expressions(vc, "", move, "VC")
    mel.connect_material_expressions(phase, "", move, "Ph")
    excite = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -800, 600)
    excite.set_editor_property("parameter_name", "Excite")
    mel.connect_material_expressions(excite, "", move, "E")
    time = mel.create_material_expression(m, unreal.MaterialExpressionTime, -800, 660)
    mel.connect_material_expressions(time, "", move, "T")
    world = mel.create_material_expression(m, unreal.MaterialExpressionTransform, -200, 500)
    world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    mel.connect_material_expressions(move, "", world, "")
    mel.connect_material_property(world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    finish(m)


logo = import_texture("T_GrassLogo")
ads = import_texture("T_Ads")
ground("M_Grass", GRASS, logo)
mark = import_texture("T_Mark")
ground("M_Pitch", PITCH, params=[("Green", 0.0), ("Dust", 0.0), ("Wear", 0.3), ("RoughSpots", 0.3)], marks=mark)
led(ads)
screen(ads)
import_fan()
crowd()
flag()
