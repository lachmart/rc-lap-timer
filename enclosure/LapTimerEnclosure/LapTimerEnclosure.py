"""RC Lap Timer – 3D-printable enclosure, built by a Fusion 360 script.

Fusion: Utilities → Add-Ins → Scripts and Add-Ins → "+" (My Scripts) → pick this folder → Run.
It opens a new design with five components, laid out side by side for printing:

  Timer box          frame whose bottom is the ESP32 pin plate (prints plate-down on the bed), USB slot,
                     LDR tube inside behind a small opening in the front wall
  Timer top cover    pushes into the frame from above
  Timer bottom tray  the room under the pin plate for the Dupont plugs and wires; the frame sits in it
                     on a ledge (lift the frame out to plug wires onto the pins)
  Laser post         2×AA holder pocket, switch hole, laser bore with two aiming screws
  Laser lid          covers the battery pocket

All sizes are in the P dictionary below, in mm. Measure your parts with a caliper, change the numbers and
run the script again (it makes a new design each time). The ones marked MEASURE matter most.
Both parts stand on the ground; the beam is BEAM_H above it on both.
"""
import adsk.core
import adsk.fusion
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

    # timer box
    'wall': 2.0,
    'plate_t': 1.6,        # pin plate: thin, so the pins stick out below for Dupont jumpers
    'under': 16.0,         # room under the plate for Dupont plugs and wires
    'head': 2.0,           # room above the tallest part
    'ldr_zone': 22.0,      # inner length past the board for the LDR, resistor, wires
    'side_gap': 3.0,       # room beside the board

    # LDR tube inside the box, behind an opening in the front wall (light comes in only along the beam)
    'tube_len': 22.0,      # from the inside of the front wall inwards; the LDR sits at its inner end
    'tube_d': 11.0,
    'ldr_hole': 5.4,       # LDR 5 mm (GL5528): snug
    'funnel_d': 7.6,       # opening in the outside of the front wall (keeps a rim above the frame's bottom edge)
    'funnel_len': 4.0,     # cone from the opening down to the LDR hole

    # covers
    'cover_t': 2.0,
    'lip_h': 3.0,
    'lip_t': 1.6,
    'fit': 0.25,           # gap between a lip and the wall it goes into

    # laser post (MEASURE)
    'holder_l': 58.0,      # 2×AA holder, outside
    'holder_w': 32.0,
    'holder_h': 16.0,
    'switch_d': 20.0,      # round rocker switch, panel hole
    'switch_room': 18.0,   # room behind the switch inside the post
    'laser_d': 6.0,        # laser module diameter
    'laser_clear': 1.0,    # bore = laser_d + this: room to aim with the two screws
    'screw_d': 2.6,        # hole for an M3 screw to cut its own thread (aiming)
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


def cover(root, name, ox, oy, L, W, inner_x0, inner_y0, inner_x1, inner_y1):
    """A flat cover L×W with a hollow lip that fits inside the opening (inner_*), lying lip-up for printing."""
    c = new_component(root, name)
    t, lip_h, lip_t, fit = P['cover_t'], P['lip_h'], P['lip_t'], P['fit']
    b = block(c, ox, oy, 0, ox + L, oy + W, t)
    x0, y0 = ox + inner_x0 + fit, oy + inner_y0 + fit
    x1, y1 = ox + inner_x1 - fit, oy + inner_y1 - fit
    block(c, x0, y0, t, x1, y1, t + lip_h, JOIN, b)
    block(c, x0 + lip_t, y0 + lip_t, t, x1 - lip_t, y1 - lip_t, t + lip_h, CUT, b)
    b.name = name
    return c


def tray(root, name, oy, L, W, plate_z):
    """The bottom: a tray as tall as the room under the pin plate. The frame (L×W) stands on a ledge inside
    it, and a rim lip_h high goes around the frame's bottom edge."""
    p = P
    w, fit, lip_h = p['wall'], p['fit'], p['lip_h']
    t = w + fit
    c = new_component(root, name)
    b = block(c, -t, oy - t, 0, L + t, oy + W + t, plate_z + lip_h)
    block(c, -fit, oy - fit, plate_z, L + fit, oy + W + fit, plate_z + lip_h, CUT, b)   # socket for the frame
    block(c, w, oy + w, p['cover_t'], L - w, oy + W - w, plate_z, CUT, b)               # room for the wires
    b.name = name
    return c


def timer(root):
    p = P
    w = p['wall']
    in_l = p['usb_gap'] + p['pcb_l'] + p['ldr_zone']
    in_w = p['pcb_w'] + 2 * p['side_gap']
    L, W = in_l + 2 * w, in_w + 2 * w
    plate_z = p['cover_t'] + p['under']                 # the tray below holds the wires
    plate_top = plate_z + p['plate_t']
    pcb_z = plate_top + p['spacer']
    top = pcb_z + p['pcb_t'] + p['parts_h'] + p['head']

    # the frame starts at the pin plate, so it prints plate-down on the bed
    c = new_component(root, 'Timer box')
    b = block(c, 0, 0, plate_z, L, W, top)
    block(c, w, w, plate_z, L - w, W - w, top, CUT, b)  # hollow frame

    # pin plate under the board, across the full width; the LDR zone stays open to the tray below
    board_x0 = w + p['usb_gap']
    plate_x1 = board_x0 + p['pcb_l'] + 1.5
    block(c, w, w, plate_z, plate_x1, W - w, plate_top, JOIN, b)
    cy = W / 2
    sk = sketch_at(c, 'z', plate_z)
    for row in (-1, 1):
        for i in range(int(p['pins'])):
            x = board_x0 + p['pin_x0'] + i * p['pitch']
            y = cy + row * p['row_gap'] / 2
            sk.sketchCurves.sketchCircles.addByCenterRadius(to_sk(sk, pt('z', plate_z, x, y)), cm(p['pin_hole'] / 2))
    extrude(c, sk, 'z', p['plate_t'], CUT, b)

    # USB: a slot open to the top of the end wall, so the board goes in from above with the cable on
    usb_z = pcb_z + p['pcb_t'] + 1.5
    block(c, 0, cy - p['usb_w'] / 2, usb_z - p['usb_h'] / 2, w, cy + p['usb_w'] / 2, top, CUT, b)

    # LDR tube inside, from the front wall (y = 0) inwards, centred on the beam; the light comes in through
    # a small funnel-shaped opening in the wall
    tube_x = board_x0 + p['pcb_l'] + p['ldr_zone'] / 2
    bz = p['beam_h']
    rod(c, 'y', w, w + p['tube_len'], tube_x, bz, p['tube_d'], JOIN, b)
    rod(c, 'y', -1, w + p['tube_len'] + 1, tube_x, bz, p['ldr_hole'], CUT, b)
    lo = c.features.loftFeatures
    s1 = sketch_at(c, 'y', 0)
    s1.sketchCurves.sketchCircles.addByCenterRadius(to_sk(s1, pt('y', 0, tube_x, bz)), cm(p['funnel_d'] / 2))
    s2 = sketch_at(c, 'y', p['funnel_len'])
    s2.sketchCurves.sketchCircles.addByCenterRadius(to_sk(s2, pt('y', p['funnel_len'], tube_x, bz)),
                                                    cm(p['ldr_hole'] / 2))
    li = lo.createInput(CUT)
    li.loftSections.add(s1.profiles.item(0))
    li.loftSections.add(s2.profiles.item(0))
    li.isSolid = True
    li.participantBodies = [b]
    lo.add(li)
    # trim anything below the pin plate (the bottom of the tube) so the frame stands flat on the bed
    block(c, -5, -5, plate_z - 30, L + 5, W + 5, plate_z, CUT, b)
    b.name = 'Timer box'

    # top cover and bottom tray, beside the box for printing
    cover(root, 'Timer top cover', 0, W + 15, L, W, w, w, L - w, W - w)
    tray(root, 'Timer bottom tray', 2 * W + 35, L, W, plate_z)
    return L


def laser(root, ox):
    p = P
    w = p['wall']
    pocket_l = p['switch_room'] + p['holder_l'] + 1.0
    in_w = p['holder_w'] + 1.0
    W = in_w + 2 * w
    pocket_x1 = ox + w + pocket_l
    L = w + pocket_l + p['tower']
    H = p['post_h']
    cy = W / 2

    c = new_component(root, 'Laser post')
    b = block(c, ox, 0, 0, ox + L, W, H)
    block(c, ox + w, w, w, pocket_x1, W - w, H, CUT, b)      # battery + switch pocket, open on top

    # switch: round hole in the end wall, behind it the switch room
    rod(c, 'x', ox - 1, ox + w + 1, cy, w + p['switch_d'] / 2 + 2, p['switch_d'], CUT, b)

    # laser bore across the tower at beam height, along y (towards the timer), with room to aim
    lx = pocket_x1 + p['tower'] / 2
    bz = p['beam_h']
    rod(c, 'y', -1, W + 1, lx, bz, p['laser_d'] + p['laser_clear'], CUT, b)
    # aiming screws: one from the top (up/down), one from the end (left/right); M3 cuts its own thread
    rod(c, 'z', bz, H + 1, lx, cy, p['screw_d'], CUT, b)
    rod(c, 'x', lx, ox + L + 1, cy, bz, p['screw_d'], CUT, b)
    # wire channel from the bore to the pocket
    block(c, pocket_x1 - 1, cy - 2, bz - 2, lx, cy + 2, bz + 2, CUT, b)
    b.name = 'Laser post'

    cover(root, 'Laser lid', ox, W + 15, L, W, w, w, w + pocket_l, W - w)


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
