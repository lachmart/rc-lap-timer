"""RC Lap Timer – 3D-printable enclosure, built by a Fusion 360 script.

Fusion: Utilities → Add-Ins → Scripts and Add-Ins → LapTimerEnclosure → Run. It opens a new design with
five parts, each already lying the way it prints (flat side down, no supports needed):

  Timer base      the tray: beam opening + LDR tube, posts for the pin plate, room for the wiring and Dupont
                  plugs, USB slot in the end wall
  Pin plate       a flat plate with the ESP32's two pin rows; screwed onto the posts (4× M3×6)
  Timer lid       flat lid with a lip, a groove on top marks the beam line
  Laser post      2×AA holder pocket and room for the switch, the laser in a bore at beam height with two
                  aiming screws (M3, top and side)
  Laser lid       flat lid with the switch hole, a hole to reach the top aiming screw, beam groove

How it prints well: every part has a flat face on the bed; holes across a part (laser, LDR) are teardrops
(pointed top, 45°) so they print without supports; the pin holes print vertical in their own flat plate; the
switch hole is in a lid, printed flat; no thin walls (2.4 mm). Screws: M3 self-tapping into 2.5 mm holes.

All sizes are in P below, in mm. Measure your parts with a caliper, change the numbers and run again (it
makes a new design each time). The ones marked MEASURE matter most. Both parts stand on the ground; the
beam is beam_h above it on both.
"""
import adsk.core
import adsk.fusion
import math
import traceback

P = {
    # the beam
    'beam_h': 22.0,        # beam above the ground (Losi Micro-B 1:24: hits the chassis, not only the wheels)

    # ESP32 DevKit, 30 pins (MEASURE)
    'pcb_l': 51.5,         # board length, USB end to antenna end
    'pcb_w': 28.5,         # board width
    'row_gap': 25.4,       # pin row to pin row, centre to centre
    'pin_x0': 7.0,         # USB end of the board to the centre of the first pin
    'pins': 15,            # pins per row
    'pitch': 2.54,
    'pin_hole': 1.0,       # printed hole for a 0.64 mm square pin (FDM holes come out a bit small)
    'spacer': 2.5,         # black plastic spacer of the pin headers
    'pcb_t': 1.6,
    'parts_h': 4.5,        # tallest part on top of the board (USB socket, module, buttons)
    'usb_w': 12.0,         # slot for the micro-USB plug
    'usb_h': 8.5,
    'usb_gap': 0.6,        # board's USB edge to the inner wall

    # shell
    'wall': 2.4,
    'floor': 2.4,
    'corner_r': 5.0,       # outside corner radius
    'under': 22.0,         # floor to pin plate: Dupont plugs on the pins + wires
    'head': 2.0,           # room above the tallest part on the board
    'ldr_zone': 24.0,      # inner length past the pin plate: LDR tube, resistor

    # pin plate
    'plate_t': 2.0,
    'plate_w': 44.0,       # wider than the board: its screws sit beside the board, under the spacer height
    'post_d': 7.0,

    # LDR (light comes in only along the beam)
    'tube_len': 22.0,      # from the inside of the front wall inwards; the LDR sits at its inner end
    'tube_d': 11.0,
    'ldr_hole': 5.4,       # LDR 5 mm (GL5528): snug
    'funnel_d': 9.0,       # opening in the outside of the front wall
    'funnel_len': 4.0,     # cone from the opening down to the LDR hole

    # lids
    'lid_t': 2.5,
    'lip_h': 3.0,
    'lip_t': 1.6,
    'fit': 0.2,            # gap between a lip and the wall it goes into
    'groove_w': 1.2,       # beam groove on the lids
    'groove_d': 0.6,

    # screws
    'tap_d': 2.5,          # M3 cuts its own thread in this
    'clear_d': 3.4,        # M3 passes through this

    # laser post (MEASURE)
    'holder_l': 58.0,      # 2×AA holder, outside
    'holder_w': 32.0,
    'holder_h': 16.0,
    'switch_d': 20.0,      # round rocker switch, panel hole (in the laser lid)
    'switch_room': 24.0,   # room for the switch body beside the holder
    'laser_d': 6.0,        # laser module diameter
    'laser_clear': 1.0,    # bore = laser_d + this: room to aim with the two screws
    'tower': 16.0,         # length of the block that holds the laser
    'post_h': 30.0,
}

app = None
ui = None


def cm(mm):
    return mm / 10.0


def V(mm):
    return adsk.core.ValueInput.createByReal(cm(mm))


AXES = {'x': 0, 'y': 1, 'z': 2}


def pt(axis, s, u, v):
    """Model point (mm) from the coordinate along `axis` and the two others (u, v in x→y→z order)."""
    c = [0.0, 0.0, 0.0]
    others = [i for i in range(3) if i != AXES[axis]]
    c[AXES[axis]] = s
    c[others[0]] = u
    c[others[1]] = v
    return adsk.core.Point3D.create(cm(c[0]), cm(c[1]), cm(c[2]))


def plane_at(comp, axis, s):
    """Construction plane perpendicular to `axis` at coordinate s (mm)."""
    base = {'x': comp.yZConstructionPlane, 'y': comp.xZConstructionPlane, 'z': comp.xYConstructionPlane}[axis]
    if abs(s) < 1e-9:
        return base
    for d in (s, -s):                                   # the XZ plane's normal may point to -Y
        inp = comp.constructionPlanes.createInput()
        inp.setByOffset(base, V(d))
        plane = comp.constructionPlanes.add(inp)
        o = plane.geometry.origin
        if abs([o.x, o.y, o.z][AXES[axis]] - cm(s)) < 1e-6:
            plane.isLightBulbOn = False
            return plane
        plane.deleteMe()
    raise RuntimeError('plane at %s=%s' % (axis, s))


def sketch_at(comp, axis, s):
    sk = comp.sketches.add(plane_at(comp, axis, s))
    sk.isVisible = False
    return sk


def to_sk(sk, p):
    return sk.modelToSketchSpace(p)


def all_profiles(sk):
    col = adsk.core.ObjectCollection.create()
    for i in range(sk.profiles.count):
        col.add(sk.profiles.item(i))
    return col


def extrude(comp, sk, axis, length, op, body=None, profiles=None):
    """Extrude the sketch's profiles `length` mm along +axis (negative: along -axis)."""
    n = sk.xDirection.crossProduct(sk.yDirection)
    along = [n.x, n.y, n.z][AXES[axis]]
    positive = (length > 0) == (along > 0)
    ext = comp.features.extrudeFeatures
    inp = ext.createInput(profiles or all_profiles(sk), op)
    direction = adsk.fusion.ExtentDirections.PositiveExtentDirection if positive \
        else adsk.fusion.ExtentDirections.NegativeExtentDirection
    inp.setOneSideExtent(adsk.fusion.DistanceExtentDefinition.create(V(abs(length))), direction)
    if body is not None:
        inp.participantBodies = [body]
    return ext.add(inp)


NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def block(comp, x0, y0, z0, x1, y1, z1, op=NEW, body=None):
    """Axis-aligned box from (x0,y0,z0) to (x1,y1,z1), mm."""
    sk = sketch_at(comp, 'z', z0)
    sk.sketchCurves.sketchLines.addTwoPointRectangle(to_sk(sk, pt('z', z0, x0, y0)), to_sk(sk, pt('z', z0, x1, y1)))
    f = extrude(comp, sk, 'z', z1 - z0, op, body)
    return f.bodies.item(0) if op == NEW else body


def rod(comp, axis, s0, s1, u, v, d, op, body=None):
    """Cylinder along `axis` from s0 to s1, centre (u, v) in the other two coordinates, diameter d."""
    sk = sketch_at(comp, axis, s0)
    sk.sketchCurves.sketchCircles.addByCenterRadius(to_sk(sk, pt(axis, s0, u, v)), cm(d / 2))
    f = extrude(comp, sk, axis, s1 - s0, op, body)
    return f.bodies.item(0) if op == NEW else body


def new_component(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


def rrect(sk, z, x0, y0, x1, y1, r):
    """Rounded rectangle on a sketch perpendicular to z (model mm)."""
    r = max(0.0, min(r, (x1 - x0) / 2 - 0.01, (y1 - y0) / 2 - 0.01))
    P_ = lambda x, y: to_sk(sk, pt('z', z, x, y))
    if r < 0.05:
        sk.sketchCurves.sketchLines.addTwoPointRectangle(P_(x0, y0), P_(x1, y1))
        return
    lines, arcs = sk.sketchCurves.sketchLines, sk.sketchCurves.sketchArcs
    k = r * (1 - math.sqrt(0.5))
    lines.addByTwoPoints(P_(x0 + r, y0), P_(x1 - r, y0))
    arcs.addByThreePoints(P_(x1 - r, y0), P_(x1 - k, y0 + k), P_(x1, y0 + r))
    lines.addByTwoPoints(P_(x1, y0 + r), P_(x1, y1 - r))
    arcs.addByThreePoints(P_(x1, y1 - r), P_(x1 - k, y1 - k), P_(x1 - r, y1))
    lines.addByTwoPoints(P_(x1 - r, y1), P_(x0 + r, y1))
    arcs.addByThreePoints(P_(x0 + r, y1), P_(x0 + k, y1 - k), P_(x0, y1 - r))
    lines.addByTwoPoints(P_(x0, y1 - r), P_(x0, y0 + r))
    arcs.addByThreePoints(P_(x0, y0 + r), P_(x0 + k, y0 + k), P_(x0 + r, y0))


def rblock(comp, x0, y0, z0, x1, y1, z1, r, op=NEW, body=None):
    """Box with rounded vertical edges."""
    sk = sketch_at(comp, 'z', z0)
    rrect(sk, z0, x0, y0, x1, y1, r)
    f = extrude(comp, sk, 'z', z1 - z0, op, body)
    return f.bodies.item(0) if op == NEW else body


def teardrop(sk, y, x, z, d):
    """Teardrop (pointed top, 45°) on a sketch perpendicular to y: a hole across a part prints without supports."""
    r = d / 2
    a = r * math.sqrt(0.5)
    P_ = lambda u, v: to_sk(sk, pt('y', y, u, v))
    sk.sketchCurves.sketchArcs.addByThreePoints(P_(x - a, z + a), P_(x, z - r), P_(x + a, z + a))
    sk.sketchCurves.sketchLines.addByTwoPoints(P_(x + a, z + a), P_(x, z + r * math.sqrt(2)))
    sk.sketchCurves.sketchLines.addByTwoPoints(P_(x, z + r * math.sqrt(2)), P_(x - a, z + a))


def bore_y(comp, y0, y1, x, z, d, body):
    """Teardrop hole along y."""
    sk = sketch_at(comp, 'y', y0)
    teardrop(sk, y0, x, z, d)
    extrude(comp, sk, 'y', y1 - y0, CUT, body)


def funnel_y(comp, y0, length, x, z, d0, d1, body):
    """Teardrop cone along y from d0 at y0 to d1 at y0+length: the beam's way in."""
    lo = comp.features.loftFeatures
    s0 = sketch_at(comp, 'y', y0)
    teardrop(s0, y0, x, z, d0)
    s1 = sketch_at(comp, 'y', y0 + length)
    teardrop(s1, y0 + length, x, z, d1)
    li = lo.createInput(CUT)
    li.loftSections.add(s0.profiles.item(0))
    li.loftSections.add(s1.profiles.item(0))
    li.isSolid = True
    li.participantBodies = [body]
    lo.add(li)


def lid(root, name, ox, oy, L, W, opening, holes=(), groove_x=None):
    """Flat lid L×W lying top-down (as it prints) with a lip that goes into `opening` (x0, y0, x1, y1, r) of
    the part below. holes: (x, y, d) through the lid, groove_x: the beam line, marked on top. x, y are in
    the part's own coordinates; the lid is flipped over y, which is symmetric for everything here."""
    p = P
    c = new_component(root, name)
    b = rblock(c, ox, oy, 0, ox + L, oy + W, p['lid_t'], p['corner_r'])
    x0, y0, x1, y1, r = opening
    f = p['fit']
    rblock(c, ox + x0 + f, oy + y0 + f, p['lid_t'], ox + x1 - f, oy + y1 - f, p['lid_t'] + p['lip_h'], r - f, JOIN, b)
    t = p['lip_t']
    rblock(c, ox + x0 + f + t, oy + y0 + f + t, p['lid_t'], ox + x1 - f - t, oy + y1 - f - t,
           p['lid_t'] + p['lip_h'], max(r - f - t, 0.5), CUT, b)
    for x, y, d in holes:
        rod(c, 'z', -1, p['lid_t'] + p['lip_h'] + 1, ox + x, oy + W - y, d, CUT, b)
    if groove_x is not None:
        g = p['groove_w'] / 2
        block(c, ox + groove_x - g, oy - 1, -1, ox + groove_x + g, oy + W + 1, p['groove_d'], CUT, b)
    b.name = name
    return c


def timer(root):
    p = P
    w, fl, R = p['wall'], p['floor'], p['corner_r']
    r_in = R - w
    # inside, along x: USB end wall | pin plate (board on it) | LDR zone
    plate_x0 = w + 0.5
    board_x0 = w + p['usb_gap']
    plate_x1 = board_x0 + p['pcb_l'] + 1.0
    in_l = (plate_x1 - w) + p['ldr_zone']
    in_w = p['plate_w'] + 1.0
    L, W = in_l + 2 * w, in_w + 2 * w
    cy = W / 2
    plate_z = fl + p['under']
    pcb_z = plate_z + p['plate_t'] + p['spacer']
    top = pcb_z + p['pcb_t'] + p['parts_h'] + p['head']
    bz = p['beam_h']

    # ---- base: rounded tray
    c = new_component(root, 'Timer base')
    b = rblock(c, 0, 0, 0, L, W, top, R)
    rblock(c, w, w, fl, L - w, W - w, top + 1, r_in, CUT, b)

    # posts for the pin plate: screws beside the board (under the spacer height)
    sx = (plate_x0 + 4, plate_x1 - 4)
    sy = (cy - p['plate_w'] / 2 + 3.5, cy + p['plate_w'] / 2 - 3.5)
    pr = p['post_d'] / 2
    for x in sx:
        for y in sy:
            rod(c, 'z', fl - 0.1, plate_z, x, y, p['post_d'], JOIN, b)
            # tie the post to the side wall (no thin gap between them, and stiffer)
            y0, y1 = (w - 0.1, y) if y < cy else (y, W - w + 0.1)
            block(c, x - pr, y0, fl - 0.1, x + pr, y1, plate_z, JOIN, b)
            rod(c, 'z', plate_z - 8, plate_z + 1, x, y, p['tap_d'], CUT, b)

    # LDR tube: a block standing on the floor, the teardrop bore along the beam, a funnel at the wall
    tube_x = plate_x1 + p['ldr_zone'] / 2
    tr = p['tube_d'] / 2
    block(c, tube_x - tr, w - 0.1, fl - 0.1, tube_x + tr, w + p['tube_len'], bz + tr + 1, JOIN, b)
    bore_y(c, -1, w + p['tube_len'] + 1, tube_x, bz, p['ldr_hole'], b)
    funnel_y(c, 0, p['funnel_len'], tube_x, bz, p['funnel_d'], p['ldr_hole'], b)

    # USB: a slot from the top edge of the end wall down to the plug
    usb_z = pcb_z + p['pcb_t'] + 1.5
    block(c, -1, cy - p['usb_w'] / 2, usb_z - p['usb_h'] / 2, w + 1, cy + p['usb_w'] / 2, top + 1, CUT, b)
    b.name = 'Timer base'

    # ---- pin plate: flat, so the pin holes print vertical
    oy = W + 12
    cp = new_component(root, 'Pin plate')
    pb = rblock(cp, plate_x0, oy, 0, plate_x1, oy + p['plate_w'], p['plate_t'], 3.0)
    sk = sketch_at(cp, 'z', 0)
    for row in (-1, 1):
        for i in range(int(p['pins'])):
            x = board_x0 + p['pin_x0'] + i * p['pitch']
            y = oy + p['plate_w'] / 2 + row * p['row_gap'] / 2
            sk.sketchCurves.sketchCircles.addByCenterRadius(to_sk(sk, pt('z', 0, x, y)), cm(p['pin_hole'] / 2))
    for x in sx:
        for y in sy:
            sk.sketchCurves.sketchCircles.addByCenterRadius(
                to_sk(sk, pt('z', 0, x, oy + y - (cy - p['plate_w'] / 2))), cm(p['clear_d'] / 2))
    extrude(cp, sk, 'z', p['plate_t'], CUT, pb)
    pb.name = 'Pin plate'

    # ---- lid
    lid(root, 'Timer lid', 0, oy + p['plate_w'] + 12, L, W, (w, w, L - w, W - w, r_in), groove_x=tube_x)
    return L


def laser(root, ox):
    p = P
    w, fl, R = p['wall'], p['floor'], p['corner_r']
    r_in = R - w
    pocket_l = p['switch_room'] + p['holder_l'] + 1.0
    in_w = p['holder_w'] + 1.0
    W = in_w + 2 * w
    cy = W / 2
    px1 = w + pocket_l
    L = px1 + p['tower']
    H = p['post_h']
    bz = p['beam_h']
    lx = px1 + p['tower'] / 2

    c = new_component(root, 'Laser post')
    b = rblock(c, ox, 0, 0, ox + L, W, H, R)
    rblock(c, ox + w, w, fl, ox + px1, W - w, H + 1, r_in, CUT, b)   # switch room + battery holder
    # laser bore across the tower at beam height (teardrop), room to aim
    bore_y(c, -1, W + 1, ox + lx, bz, p['laser_d'] + p['laser_clear'], b)
    # aiming screws: one from the top (up/down), one from the end (left/right)
    rod(c, 'z', bz, H + 1, ox + lx, cy, p['tap_d'], CUT, b)
    rod(c, 'x', ox + lx, ox + L + 1, cy, bz, p['tap_d'], CUT, b)
    # wire channel from the bore into the pocket (a short 4 mm bridge prints fine)
    sk = sketch_at(c, 'y', cy - 2)
    pts = [(px1 - 1, bz - 2), (lx, bz - 2), (lx, bz + 2), (px1 - 1, bz + 2)]
    for i in range(4):
        (x0, z0), (x1, z1) = pts[i], pts[(i + 1) % 4]
        sk.sketchCurves.sketchLines.addByTwoPoints(to_sk(sk, pt('y', cy - 2, ox + x0, z0)), to_sk(sk, pt('y', cy - 2, ox + x1, z1)))
    extrude(c, sk, 'y', 4, CUT, b)
    b.name = 'Laser post'

    lid(root, 'Laser lid', ox, W + 12, L, W, (w, w, px1, W - w, r_in),
        holes=[(w + p['switch_room'] / 2, cy, p['switch_d']), (lx, cy, p['clear_d'] + 0.6)], groove_x=lx)


def run(context):
    global app, ui
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        L = timer(root)
        laser(root, L + 20)
        app.activeViewport.fit()
    except Exception:
        if ui:
            ui.messageBox('Lap timer enclosure failed:\n{}'.format(traceback.format_exc()))
