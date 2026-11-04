"""RC Lap Timer – 3D-printable enclosure, built by a Fusion 360 script.

Fusion: Utilities → Add-Ins → Scripts and Add-Ins → LapTimerEnclosure → Run. It opens a new design with
five parts, each already lying the way it prints (flat side down, no supports needed):

  Timer base   the ESP32 sits on two cross-ribs (pegs through its mounting holes), pins down, room for
               the soldered wires; the LDR sits in a solid block in the corner
               behind the beam opening (push it in from inside); USB slot
  Timer lid    flat, a lip all the way round the open part, four short tubes underneath that hold the
               board down around its holes (nothing flexes, nothing to snap); a groove on top marks the beam
  Laser post   the laser sits in a solid block at one end (push it in from the front, its wires go through
               a channel into the pocket), two M3 aiming screws (top, end); 2×AA holder; switch zone
  Laser lid    like the timer's, with the switch hole and a hole to reach the top aiming screw
  Sleeve       for transport: put the two parts bottom to bottom and slide the sleeve over the joint

Assembly: timer – set the board on the pegs, push the LDR in, solder 3V3/GND/D32 + the resistor, press the
lid on (it holds the board).
Laser post – drop the holder in, push the laser in, wire the switch in the lid, press the lid on, aim.

Both parts have the same footprint and height (the larger of what each needs), so they make a neat pair and
the sleeve fits both. Standing face to face (one turned half way) the laser lines up with the LDR, and the
lid grooves line up. The height is set by the beam (beam_h + the bore + 1 mm): lowering beam_h lowers both.

How it prints well: every part has a flat face on the bed; the laser and LDR blocks stand on the floor (no
spans); holes across a part are teardrops with a small flat top (no supports, no droop, little height);
walls 2.4 mm everywhere, never thinned. M3 screws self-tap into 2.5 mm holes.

All sizes are in P below, in mm. Change the numbers and run again (it makes a new design each time).
"""
import adsk.core
import adsk.fusion
import math
import traceback

P = {
    # the beam
    'beam_h': 22.0,        # beam above the ground (Losi Micro-B 1:24: hits the chassis, not only the wheels)

    # ESP32 DevKit, 30 pins. Holes measured on Martin's board (the DOIT drawing has 47.4 × 23.8)
    'pcb_l': 51.5,         # board length, USB end to antenna end (edges aren't cut precisely)
    'pcb_w': 28.2,         # board width
    'hole_dx': 46.6,       # mounting holes: centre to centre along the board
    'hole_dy': 23.3,       #                 centre to centre across the board
    'hole_x0': 2.45,       # USB edge to the nearest holes' centres: the holes centred on the board
    'hole_d': 2.8,         # mounting hole diameter; the pegs are 0.4 mm thinner
    'press_od': 4.8,       # tubes under the timer lid that hold the board down around its holes,
    'press_pre': 0.1,      #   how much they press on it
    'pins_below': 8.5,     # header pins + their spacer below the board
    'pcb_t': 1.6,
    'parts_h': 4.5,        # tallest part on top of the board (USB socket, module, buttons)
    'usb_w': 12.0,         # slot for the micro-USB plug
    'usb_h': 8.5,
    'usb_gap': 0.6,        # board's USB edge to the inner wall

    # laser post (MEASURE)
    'holder_l': 58.0,      # 2×AA holder, outside
    'holder_w': 32.0,
    'holder_h': 16.0,
    'switch_d': 20.0,      # round rocker switch, panel hole (in the laser lid)
    'switch_depth': 16.0,  # switch body below the lid
    'switch_zone': 22.0,   # inner length beside the holder for the switch body
    'laser_d': 6.0,        # laser module diameter
    'laser_clear': 0.6,    # bore = laser_d + this: room to aim with the two screws
    'laser_len': 12.0,     # laser module length (MEASURE); the aiming screws press on its middle
    'block_l': 11.0,       # the solid block that holds the laser, along the post. Its middle is the beam
                           # line; the timer's LDR is the same distance from its far end

    # shell (both parts share the footprint and the height)
    'wall': 2.4,
    'floor': 2.4,
    'corner_r': 3.0,       # outside corner radius (the beam openings near the ends stay on the flat walls)
    'head': 2.5,           # room above the tallest part on the board
    'flat': 0.4,           # flat top of the teardrop holes, above the circle
    'rib_t': 4.0,          # cross-ribs under the board's ends
    'wire_gap': 10.0,      # gap in the antenna-end rib for the wires

    # LDR (light comes in only along the beam)
    'ldr_zone': 13.0,      # at least this inner length past the board for the LDR block
    'wire_room': 5.0,      # below the pins' tips: soldered wires, the resistor
    'tube_len': 20.0,      # from the inside of the front wall inwards; the LDR sits at its inner end
    'tube_d': 8.0,
    'ldr_hole': 5.4,       # LDR 5 mm (GL5528): snug
    'funnel_d': 7.0,       # the beam's way in: a funnel from the front wall to the LDR hole
    'funnel_len': 3.0,     # cone from the opening down to the LDR hole

    # lids
    'lid_t': 2.5,
    'lip_h': 2.0,
    'lip_t': 1.6,
    'fit': 0.2,            # gap between a lip and the wall it goes into
    'groove_w': 1.2,       # beam groove on the lids
    'groove_d': 0.6,

    # transport sleeve
    'sleeve_h': 20.0,      # 10 mm over each part
    'sleeve_t': 2.0,
    'sleeve_fit': 0.25,    # per side: snug, slides on with a little force

    # screws
    'tap_d': 2.5,          # M3 cuts its own thread in this
    'clear_d': 3.4,        # M3 passes through this
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
    """Teardrop on a sketch perpendicular to y: a hole across a part prints without supports. Its 45° sides
    end in a small flat top (P['flat'] above the circle), a short bridge that saves height."""
    r = d / 2
    a = r * math.sqrt(0.5)
    top = z + r + P['flat']
    e = r * math.sqrt(2) - r - P['flat']                # half width of the flat top
    P_ = lambda u, v: to_sk(sk, pt('y', y, u, v))
    lines = sk.sketchCurves.sketchLines
    sk.sketchCurves.sketchArcs.addByThreePoints(P_(x - a, z + a), P_(x, z - r), P_(x + a, z + a))
    if e > 0.05:
        lines.addByTwoPoints(P_(x + a, z + a), P_(x + e, top))
        lines.addByTwoPoints(P_(x + e, top), P_(x - e, top))
        lines.addByTwoPoints(P_(x - e, top), P_(x - a, z + a))
    else:
        lines.addByTwoPoints(P_(x + a, z + a), P_(x, z + r * math.sqrt(2)))
        lines.addByTwoPoints(P_(x, z + r * math.sqrt(2)), P_(x - a, z + a))


def hole_top(d):
    """Top of a teardrop hole of diameter d above its centre."""
    return min(d / 2 + P['flat'], d / 2 * math.sqrt(2))


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


def lid(root, name, ox, oy, L, W, opening, holes=(), groove_x=None, pressers=()):
    """Flat lid L×W lying top-down (as it prints) with a lip that goes into `opening` (x0, y0, x1, y1, r) of
    the part below. holes: (x, y, d) through the lid, groove_x: the beam line, marked on top. x, y are in
    the part's own coordinates; the lid is flipped over y, which is symmetric for everything here.
    pressers: (x, y, length, od, id) tubes on the underside that hold something down."""
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
    for x, y, length, od, idia in pressers:
        rod(c, 'z', p['lid_t'] - 0.1, p['lid_t'] + length, ox + x, oy + W - y, od, JOIN, b)
        rod(c, 'z', p['lid_t'] + 0.5, p['lid_t'] + length + 1, ox + x, oy + W - y, idia, CUT, b)
    if groove_x is not None:                            # on the bed face, which is the top once flipped
        g = p['groove_w'] / 2
        block(c, ox + groove_x - g, oy - 1, -1, ox + groove_x + g, oy + W + 1, p['groove_d'], CUT, b)
    b.name = name
    return c


def bore_r():
    return (P['laser_d'] + P['laser_clear']) / 2


def beam_x():
    """Beam line from the laser post's end wall (outside). Standing face to face (one turned half way), the
    timer's LDR is the same distance from its other end, so the beam goes straight across."""
    return P['wall'] + P['block_l'] / 2


def footprint():
    """Shared outer size L×W×H of both parts: each fits what it needs, the larger wins."""
    p = P
    w = p['wall']
    timer_l = p['usb_gap'] + p['pcb_l'] + p['ldr_zone']
    laser_l = p['block_l'] + p['holder_l'] + 1.0 + p['switch_zone']
    in_l = max(timer_l, laser_l)
    in_w = max(p['pcb_w'] + 2.0, p['holder_w'] + 1.0)
    lip = p['lip_h'] + 0.3
    timer_h = p['floor'] + p['wire_room'] + p['pins_below'] + p['pcb_t'] + p['parts_h'] + p['head']
    ldr_h = p['beam_h'] + max(hole_top(p['ldr_hole']), hole_top(p['funnel_d'])) + 1.0
    laser_h = p['beam_h'] + hole_top(2 * bore_r()) + 1.0
    switch_h = p['floor'] + p['switch_depth'] + 1.0
    holder_h = p['floor'] + p['holder_h'] + lip + 0.2            # the lip goes along the holder's edges
    if p['block_l'] < 2 * bore_r() + 4.0:
        raise ValueError('block_l is too small for the laser bore: at least {:.1f} mm.'.format(2 * bore_r() + 4.0))
    return in_l + 2 * w, in_w + 2 * w, max(timer_h, ldr_h, laser_h, switch_h, holder_h)


def timer(root, L, W, H):
    p = P
    w, fl, R = p['wall'], p['floor'], p['corner_r']
    r_in = R - w
    cy = W / 2
    pcb_z = H - (p['pcb_t'] + p['parts_h'] + p['head'])     # board bottom: as high as the lid allows
    board_x0 = w + p['usb_gap']
    board_x1 = board_x0 + p['pcb_l']
    bz = p['beam_h']

    # ---- base: rounded tray
    c = new_component(root, 'Timer base')
    b = rblock(c, 0, 0, 0, L, W, H, R)
    rblock(c, w, w, fl, L - w, W - w, H + 1, r_in, CUT, b)

    # two solid cross-ribs under the board's ends, with pegs into its mounting holes (the lid holds the board
    # down); the pins point down between them. The rib at the antenna end has a gap for the wires.
    rt = p['rib_t'] / 2
    hx = (board_x0 + p['hole_x0'], board_x0 + p['hole_x0'] + p['hole_dx'])
    gy = p['wire_gap'] / 2
    block(c, hx[0] - rt, w - 0.1, fl - 0.1, hx[0] + rt, W - w + 0.1, pcb_z, JOIN, b)
    block(c, hx[1] - rt, w - 0.1, fl - 0.1, hx[1] + rt, cy - gy, pcb_z, JOIN, b)
    block(c, hx[1] - rt, cy + gy, fl - 0.1, hx[1] + rt, W - w + 0.1, pcb_z, JOIN, b)
    for x in hx:
        for y in (cy - p['hole_dy'] / 2, cy + p['hole_dy'] / 2):
            rod(c, 'z', pcb_z - 0.1, pcb_z + p['pcb_t'] + 0.8, x, y, p['hole_d'] - 0.4, JOIN, b)

    # LDR: a solid block in the corner (front wall, end wall, floor), the LDR goes in from inside, the beam
    # comes in through a funnel in the front wall
    tube_x = L - beam_x()
    tr = p['tube_d'] / 2
    if p['funnel_d'] / 2 + R + 0.3 > L - tube_x:
        raise ValueError('The beam opening would run into the rounded corner: make corner_r smaller or block_l bigger.')
    if tube_x - tr < board_x1 + 2.5:
        raise ValueError('The LDR block does not fit past the board: make ldr_zone bigger.')
    block(c, tube_x - tr, w - 0.1, fl - 0.1, L - w + 0.1, w + p['tube_len'], H, JOIN, b)
    bore_y(c, -1, w + p['tube_len'] + 1, tube_x, bz, p['ldr_hole'], b)
    funnel_y(c, -1, w + 1 + p['funnel_len'], tube_x, bz, p['funnel_d'], p['ldr_hole'], b)

    # USB: a slot from the top edge of the end wall down to the plug
    usb_z = pcb_z + p['pcb_t'] + 1.5
    block(c, -1, cy - p['usb_w'] / 2, usb_z - p['usb_h'] / 2, w + 1, cy + p['usb_w'] / 2, H + 1, CUT, b)
    b.name = 'Timer base'

    # ---- lid: its lip goes all the way round the open part (the LDR block's top carries the lid there)
    # a tube around each peg's tip presses on the board around its hole
    plen = H - (pcb_z + p['pcb_t']) + p['press_pre']
    press = [(x, y, plen, p['press_od'], p['hole_d'] + 0.4)
             for x in hx for y in (cy - p['hole_dy'] / 2, cy + p['hole_dy'] / 2)]
    lid(root, 'Timer lid', 0, W + 10, L, W, (w, w, tube_x - tr - 0.3, W - w, r_in), groove_x=tube_x,
        pressers=press)


def laser(root, ox, L, W, H):
    p = P
    w, fl, R = p['wall'], p['floor'], p['corner_r']
    r_in = R - w
    cy = W / 2
    bz = p['beam_h']
    lx = beam_x()
    bl = p['block_l']
    sy = w + p['laser_len'] / 2                         # the aiming screws press on the middle of the laser

    # ---- post: inside along x the solid laser block | battery holder | switch zone
    c = new_component(root, 'Laser post')
    b = rblock(c, ox, 0, 0, ox + L, W, H, R)
    rblock(c, ox + w + bl, w, fl, ox + L - w, W - w, H + 1, r_in, CUT, b)
    # the laser goes in from the front (beam side); the bore stops at the back wall, which stays closed
    bore_y(c, -1, W - w, ox + lx, bz, 2 * bore_r(), b)
    # its wires: from the back of the bore through a channel into the pocket
    block(c, ox + lx, W - w - 6, bz - 1.5, ox + w + bl + 0.1, W - w - 1, bz + 1.5, CUT, b)
    # aiming screws over the middle of the laser: one from the top (up/down), one through the end wall
    rod(c, 'z', bz, H + 1, ox + lx, sy, p['tap_d'], CUT, b)
    rod(c, 'x', ox - 1, ox + lx, sy, bz, p['tap_d'], CUT, b)
    b.name = 'Laser post'

    switch_x = L - w - p['fit'] - p['lip_t'] - 0.2 - p['switch_d'] / 2
    lid(root, 'Laser lid', ox, W + 10, L, W, (w + bl, w, L - w, W - w, r_in),
        holes=[(switch_x, cy, p['switch_d']), (lx, sy, p['clear_d'] + 0.6)], groove_x=lx)


def sleeve(root, ox, oy, L, W):
    """A ring that holds the two parts together bottom to bottom for transport."""
    p = P
    f, t, R = p['sleeve_fit'], p['sleeve_t'], p['corner_r']
    c = new_component(root, 'Sleeve')
    b = rblock(c, ox - f - t, oy - f - t, 0, ox + L + f + t, oy + W + f + t, p['sleeve_h'], R + f + t)
    rblock(c, ox - f, oy - f, -1, ox + L + f, oy + W + f, p['sleeve_h'] + 1, R + f, CUT, b)
    b.name = 'Sleeve'


def run(context):
    global app, ui
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        L, W, H = footprint()
        timer(root, L, W, H)
        laser(root, L + 15, L, W, H)
        sleeve(root, 2 * L + 30, 0, L, W)
        app.activeViewport.fit()
        ui.messageBox('Both parts: {:.0f} × {:.0f} × {:.0f} mm (+ {:.1f} mm lid)'.format(L, W, H, P['lid_t']))
    except Exception:
        if ui:
            ui.messageBox('Lap timer enclosure failed:\n{}'.format(traceback.format_exc()))
