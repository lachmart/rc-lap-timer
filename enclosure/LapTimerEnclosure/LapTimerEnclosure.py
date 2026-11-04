"""RC Lap Timer – 3D-printable enclosure, built by a Fusion 360 script.

Fusion: Utilities → Add-Ins → Scripts and Add-Ins → LapTimerEnclosure → Run. It opens a new design with
six parts, each already lying the way it prints (flat side down, no supports needed):

  Timer base      beam opening + LDR tube, the ESP32 board clips in: four standoffs with pegs through its
                  mounting holes, a snap hook over its antenna end (push the hook back to take it out); its
                  pins point down between the standoffs, room for the wiring underneath; USB slot
  Timer lid       flat lid with a lip; a groove on top marks the beam line
  Laser post      2×AA holder on the floor, room for the switch at the other end, two vertical grooves at
                  the holder's end for the laser carrier, the beam hole. Turned to face the timer, the laser
                  lines up with the timer's LDR opening (the lid grooves too).
  Laser carrier   holds the laser at beam height; prints standing up (the bore is vertical, so it's round),
                  drops into the post's grooves above the battery holder, the lid holds it down. Two M3
                  aiming screws: from the top (through the lid) and through the post's end wall. The laser's
                  wires leave through a slot at the back, down into the pocket.
  Laser lid       flat lid with the switch hole, a hole to reach the top aiming screw, the beam groove
  Sleeve          for transport: put the two parts bottom to bottom and slide the sleeve over the joint

Both parts have the same footprint and height (the larger of what each needs), so they make a neat pair and
the sleeve fits both. The height is set by the beam: the laser and the LDR tube sit at beam_h, just above
the battery holder; the wires under the board are soldered (3V3, GND, D32: Dupont plugs would need
about 6 mm more height).

How it prints well: every part has a flat face on the bed; holes across a part (laser, LDR) are teardrops
(pointed top, 45°); the laser carrier prints standing so its bore is round; the switch hole is in a lid,
printed flat; walls 2.4 mm. The aiming screws (M3) self-tap into 2.5 mm holes.

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

    # ESP32 DevKit, 30 pins. Holes measured on Martin's board (the DOIT drawing has 47.4 × 23.8)
    'pcb_l': 51.8,         # board length, USB end to antenna end (edges aren't cut precisely)
    'pcb_w': 28.2,         # board width
    'hole_dx': 46.6,       # mounting holes: centre to centre along the board
    'hole_dy': 23.3,       #                 centre to centre across the board
    'hole_x0': 2.6,        # USB edge to the nearest holes' centres: the holes centred on the board
    'hole_d': 2.8,         # mounting hole diameter; the pegs are 0.4 mm thinner
    'standoff_d': 5.0,
    'hook_w': 8.0,         # snap hook over the board's antenna end: width,
    'hook_t': 1.6,         #   arm thickness (springs back),
    'hook_catch': 1.0,     #   how far it reaches over the board
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
    'carrier_l': 10.0,     # laser carrier, along the post. Its middle is the beam line; the timer's LDR
                           # opening is the same distance from its far end
    'carrier_skin': 1.0,   # carrier material under the laser bore
    'carrier_groove': 1.2, # grooves in the post's front and back walls the carrier slides down
    'beam_hole': 5.0,      # hole in the laser post's front wall for the beam
    'laser_floor': 1.2,    # thinner floor in the laser post, so the holder fits under the carrier

    # shell (both parts share the footprint and the height)
    'wall': 2.4,
    'floor': 2.4,
    'corner_r': 3.0,       # outside corner radius (the beam openings near the ends stay on the flat walls)
    'head': 2.5,           # room above the tallest part on the board

    # LDR (light comes in only along the beam)
    'ldr_zone': 13.0,      # at least this inner length past the board for the LDR tube
    'wire_room': 5.0,      # below the pins' tips: soldered wires, the resistor
    'tube_len': 20.0,      # from the inside of the front wall inwards; the LDR sits at its inner end
    'tube_d': 8.0,
    'ldr_hole': 5.4,       # LDR 5 mm (GL5528): snug
    'funnel_d': 7.0,       # opening in the outside of the front wall (its teardrop top stays below the lid)
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


def lid(root, name, ox, oy, L, W, opening, holes=(), groove_x=None, notch=None):
    """Flat lid L×W lying top-down (as it prints) with a lip that goes into `opening` (x0, y0, x1, y1, r) of
    the part below. holes: (x, y, d) through the lid, groove_x: the beam line, marked on top. x, y are in
    the part's own coordinates; the lid is flipped over y, which is symmetric for everything here.
    notch: (x0, y0, x1, y1) in the part's coordinates where the lip is left out, e.g. over the LDR tube."""
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
    if notch is not None:                               # y is flipped on the lid
        nx0, ny0, nx1, ny1 = notch
        block(c, ox + nx0, oy + W - ny1, p['lid_t'], ox + nx1, oy + W - ny0, p['lid_t'] + p['lip_h'] + 1, CUT, b)
    if groove_x is not None:
        g = p['groove_w'] / 2
        block(c, ox + groove_x - g, oy - 1, -1, ox + groove_x + g, oy + W + 1, p['groove_d'], CUT, b)
    b.name = name
    return c


def bore_r():
    return (P['laser_d'] + P['laser_clear']) / 2


def tube_top():
    """Top of the LDR tube block: the teardrop bore's top + material over it."""
    return P['beam_h'] + P['ldr_hole'] / 2 * math.sqrt(2) + 1.0


def carrier_bottom():
    return P['beam_h'] - bore_r() - P['carrier_skin']


def beam_x():
    """Beam line from the laser post's end wall (outside). Standing face to face (one turned half way), the
    timer's LDR opening is the same distance from its other end, so the beam goes straight across."""
    return P['wall'] + P['carrier_l'] / 2


def footprint():
    """Shared outer size L×W×H of both parts: each fits what it needs, the larger wins."""
    p = P
    w = p['wall']
    timer_l = p['usb_gap'] + p['pcb_l'] + p['ldr_zone']
    laser_l = p['switch_zone'] + p['holder_l'] + 1.0
    in_l = max(timer_l, laser_l)
    in_w = max(p['pcb_w'] + 2.0, p['holder_w'] + 1.0)
    timer_h = p['floor'] + p['wire_room'] + p['pins_below'] + p['pcb_t'] + p['parts_h'] + p['head']
    tube_h = tube_top() + 0.3                                            # the lid's lip has a notch there
    laser_h = p['beam_h'] + bore_r() + 1.5                               # the carrier's bore + material over it
    switch_h = p['laser_floor'] + p['switch_depth'] + 1.0
    # the holder must fit under the laser carrier, the bore must fit in the carrier
    holder_top = p['laser_floor'] + p['holder_h']
    if holder_top + 0.3 > carrier_bottom():
        raise ValueError('The battery holder ({:.1f} mm) does not fit under the laser carrier ({:.1f} mm): lower '
                         'laser_floor, carrier_skin or laser_clear, or raise beam_h.'.format(holder_top, carrier_bottom()))
    if p['carrier_l'] < 2 * bore_r() + 3.0:
        raise ValueError('carrier_l is too small for the laser bore: at least {:.1f} mm.'.format(2 * bore_r() + 3.0))
    return in_l + 2 * w, in_w + 2 * w, max(timer_h, tube_h, laser_h, switch_h)


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
    # four standoffs under the board's mounting holes, with pegs through them; the pins point down between
    for x in (board_x0 + p['hole_x0'], board_x0 + p['hole_x0'] + p['hole_dx']):
        for y in (cy - p['hole_dy'] / 2, cy + p['hole_dy'] / 2):
            rod(c, 'z', fl - 0.1, pcb_z, x, y, p['standoff_d'], JOIN, b)
            rod(c, 'z', pcb_z - 0.1, pcb_z + p['pcb_t'] + 0.8, x, y, p['hole_d'] - 0.4, JOIN, b)
    # snap hook over the board's antenna end: a springy arm from the floor, a 45° lead-in on top, a catch
    # 1 mm over the board's top edge (its underside is the only overhang, 1 mm)
    a, t, k = board_x1 + 0.3, p['hook_t'], p['hook_catch']   # 0.3: board edges aren't cut precisely
    zc = pcb_z + p['pcb_t'] + 0.15                      # the catch, just above the board
    zt = zc + 0.8 + k                                   # the hook's top
    hy = p['hook_w'] / 2
    sk = sketch_at(c, 'y', cy - hy)
    poly = [(a, fl - 0.1), (a + t, fl - 0.1), (a + t, zt), (a, zt), (a - k, zt - k), (a - k, zc), (a, zc)]
    for i in range(len(poly)):
        (x0, z0), (x1, z1) = poly[i], poly[(i + 1) % len(poly)]
        sk.sketchCurves.sketchLines.addByTwoPoints(to_sk(sk, pt('y', cy - hy, x0, z0)), to_sk(sk, pt('y', cy - hy, x1, z1)))
    extrude(c, sk, 'y', p['hook_w'], JOIN, b)
    # LDR tube: a block standing on the floor, the teardrop bore along the beam, a funnel at the wall
    tube_x = L - beam_x()
    tr = p['tube_d'] / 2
    if p['funnel_d'] / 2 + R + 0.3 > L - tube_x:
        raise ValueError('The beam opening would run into the rounded corner: make corner_r smaller or carrier_l bigger.')
    if tube_x - tr < board_x1 + 1 or tube_x + tr > L - w:
        raise ValueError('The LDR tube does not fit between the board and the end wall: change carrier_l or ldr_zone.')
    # the tube block reaches the end wall (sturdier, no thin gap); the lid's lip is left out over it
    block(c, tube_x - tr, w - 0.1, fl - 0.1, L - w + 0.1, w + p['tube_len'], tube_top(), JOIN, b)
    bore_y(c, -1, w + p['tube_len'] + 1, tube_x, bz, p['ldr_hole'], b)
    funnel_y(c, 0, p['funnel_len'], tube_x, bz, p['funnel_d'], p['ldr_hole'], b)
    # USB: a slot from the top edge of the end wall down to the plug
    usb_z = pcb_z + p['pcb_t'] + 1.5
    block(c, -1, cy - p['usb_w'] / 2, usb_z - p['usb_h'] / 2, w + 1, cy + p['usb_w'] / 2, H + 1, CUT, b)
    b.name = 'Timer base'

    # ---- lid
    lid(root, 'Timer lid', 0, W + 10, L, W, (w, w, L - w, W - w, r_in), groove_x=tube_x,
        notch=(tube_x - tr - 0.5, -1, L + 1, w + p['tube_len'] + 0.5))


def laser(root, ox, L, W, H):
    p = P
    w, fl, R = p['wall'], p['laser_floor'], p['corner_r']
    r_in = R - w
    cy = W / 2
    bz = p['beam_h']
    lx = beam_x()
    cl, gd, f = p['carrier_l'], p['carrier_groove'], p['fit']
    cb = carrier_bottom()
    sy = w + p['laser_len'] / 2                         # the aiming screws press on the middle of the laser

    # ---- post: inside along x the battery holder (the carrier sits over its end at x = 0) | switch zone
    c = new_component(root, 'Laser post')
    b = rblock(c, ox, 0, 0, ox + L, W, H, R)
    rblock(c, ox + w, w, fl, ox + L - w, W - w, H + 1, r_in, CUT, b)
    # vertical grooves in the front and back walls; the carrier rests on their bottoms
    block(c, ox + w - 0.1, w - gd, cb, ox + w + cl + f, w + 0.1, H + 1, CUT, b)
    block(c, ox + w - 0.1, W - w - 0.1, cb, ox + w + cl + f, W - w + gd, H + 1, CUT, b)
    # the beam's way out through the front wall, and the side aiming screw through the end wall
    bore_y(c, -1, w - gd + 0.1, ox + lx, bz, p['beam_hole'], b)
    rod(c, 'x', ox - 1, ox + w + 0.1, sy, bz, p['clear_d'], CUT, b)
    b.name = 'Laser post'

    # ---- carrier, lying on its front face so the laser bore prints vertical. Its coordinates while printing:
    # X = along the post (x - w), Y = up (z - cb), Z = across (y - (w - gd + f))
    ch = H - cb
    clen = W - 2 * w + 2 * gd - 2 * f
    y0 = w - gd + f
    cx0, cy0 = ox + L + 15, 0
    cc = new_component(root, 'Laser carrier')
    cbody = block(cc, cx0, cy0, 0, cx0 + cl, cy0 + ch, clen)
    bx, by = cx0 + lx - w, cy0 + bz - cb               # the bore's centre
    rod(cc, 'z', -1, clen + 1, bx, by, 2 * bore_r(), CUT, cbody)
    zs = sy - y0                                        # the screws' position along the laser
    rod(cc, 'y', by, cy0 + ch + 1, bx, zs, p['tap_d'], CUT, cbody)   # top screw (up/down)
    rod(cc, 'x', cx0 - 1, bx, by, zs, p['tap_d'], CUT, cbody)        # side screw (left/right)
    # wire slot at the back end, from the bore down to the carrier's bottom
    block(cc, bx - 1.5, cy0 - 1, clen - 6, bx + 1.5, by, clen + 1, CUT, cbody)
    cbody.name = 'Laser carrier'

    switch_x = L - w - f - p['lip_t'] - 0.2 - p['switch_d'] / 2
    lid(root, 'Laser lid', ox, W + 10, L, W, (w + cl + f, w, L - w, W - w, r_in),
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
        sleeve(root, 2 * L + 60, 0, L, W)
        app.activeViewport.fit()
        ui.messageBox('Both parts: {:.0f} × {:.0f} × {:.0f} mm (+ {:.1f} mm lid)'.format(L, W, H, P['lid_t']))
    except Exception:
        if ui:
            ui.messageBox('Lap timer enclosure failed:\n{}'.format(traceback.format_exc()))
