# AxisFlow Workbench for FreeCAD

version 0.2.1 (alpha)

**AxisFlow** is a precision drawing assistant for FreeCAD, inspired by the workflow concept of **MicroStation AccuDraw**.

The goal of AxisFlow is to provide a more intuitive and efficient way of creating 2D and 3D geometry by introducing a dynamic local coordinate system, contextual axes, and guided input directly at the cursor position.

Instead of switching between tools, panels, and coordinate dialogs, AxisFlow helps users maintain focus on the modeling process by providing:

* dynamic X/Y/Z axis guidance;
* contextual local coordinate systems;
* fast distance and angle input;
* improved control of drawing direction;
* a workflow based on user intent rather than repetitive manual constraints.

AxisFlow extends the FreeCAD modeling experience with an interactive drawing approach designed for users who value speed, accuracy, and a more natural connection between mouse movement and geometric creation.

The project is inspired by the principles behind MicroStation AccuDraw, adapted for the FreeCAD environment and its parametric modeling workflow.

---

## What AxisFlow is (and is not)

AxisFlow is a reusable **coordinate-input module**, not a drawing workbench.

It supplies the input half of a drawing tool: the compass anchored at a reference point, ortho and radial modes, coordinate-field locks (freeze X, Y, Z or the polar distance/angle), a smart lock that constrains movement to the nearest axis, numeric value entry, a configurable key map, and a confirmed point. **AxisFlow never creates document objects.** No lines, no shapes. What to build from the points it hands back is entirely the consumer's decision.

That separation is the whole design. Any workbench, command, or macro can use AxisFlow through its Python API without AxisFlow knowing anything about it — the dependency runs one way only. A consumer is free to build its geometry with whatever it likes, including FreeCAD's own `Draft.make_line` / `make_wire` / `make_circle`, which live in `draftmake/*` and touch neither `Snapper` nor `DraftToolBar`.

Consequently the core ships no drawing primitives -- no Line, Rectangle, Arc or Circle -- and none are planned. What AxisFlow *does* ship are consumers built on that API, all outside the core: a demo line command (`axisflow_demo.line()`, which now also draws arc segments and can snap to itself), the Move / Copy commands (`axisflow_transform.py`) and Extrude (`axisflow_extrude.py`). They build geometry with `Draft.make_wire`, `Part.Wire`, `Draft.move` and `Part::Extrusion`, and draw their own previews with self-owned Coin nodes or Draft ghosts. They are examples of the API, not part of it.

## Why not build on Draft's input

Early versions hooked `FreeCADGui.Snapper.snap` to constrain points inside Draft, Arch and BIM tools. Geometrically that worked, but it fought Draft for the keyboard.

Reading Draft 1.1.1 made the reason plain: `FreeCADGui.Snapper` and `FreeCADGui.draftToolBar` are not one tool's widgets, they are the canonical coordinate input shared by the whole Draft family (Draft, Arch/BIM, CAM's point picker, and third-party addons built on Draft). Draft already implements axis locking (`DraftToolBar.constrain` sets `Snapper.mask`; `Snapper.constrain` projects onto the working-plane axis), in-command shortcuts (`RestrictX`/`Y`/`Z` default to X/Y/Z, `Global` to G), a typed-value latch (`setMouseMode` plus the `MouseDelay` preference), and a global/working-plane coordinate toggle. Placing a second input panel beside it means competing for focus, lock state, relativity and units — a structural conflict, not a bug to polish.

So AxisFlow owns the input inside **its own** tools and does not try to take it away from anyone else's. Sketcher is untouched by this: it has its own C++ `DrawSketchHandler` and no Draft toolbar.

An optional Draft adapter (`axisflow_snap.py`) still exists, but only as a cooperative visualiser: it shows the compass over a running Draft/BIM command and mirrors Draft's own lock. It does not constrain and it does not claim keys.

## Status

Working today: the constraint solver (ortho and radial, working-plane relative, coordinate-field locks incl. polar distance/angle, smart lock); a singleton service with signals; a Coin3D compass that orients to the working plane (contour + centre point, square in ortho / circle in radial, no Z axis, size and colours from settings); a configurable **live guide line** from the compass centre to the cursor (on/off, style and colour from settings); **live-line indexing** in the local frame (snap the line length to a remembered "indexed distance", or onto the nearest X/Y axis, shown as a candidate point / candidate axis, with the coordinate fields following the snap); a floating panel with editable numeric fields (unit suffixes are not accepted yet -- see Status), an active field chosen by the nearest axis with a highlight and Tab cycling, lock checkboxes, "just start typing" with the caret always in the active field, AccuDraw-style sign-from-direction (optional, the "Smart negative direction" setting), round-on-blur, and optional **focus advance on movement** (type X, move the mouse deliberately -- the caret is already in Y), and a **Local/World coordinate-frame** button; an optional **compass follows direction** mode (off / keep Z / full: each confirmed point re-aims the compass +X along the segment just drawn, AccuDraw's rotate-to-segment); a smooth **compass rotation animation** (slerp with ease-in/out, duration a setting, purely visual — input math never waits); a reconfigurable key map persisted to FreeCAD's parameter tree; an input state machine (Idle/Tracking/Finished, continue mode, finish-line vs cancel-command, Enter commits a point); the public **`getPoint(callback, movecallback, last, continuous, finishcallback)`** callback API; a demo Line command that draws multiple lines with a live preview and picks up the active working plane; **object snapping** that reuses FreeCAD's own snaps (endpoint, midpoint, centre, intersection, perpendicular, near, extension, parallel, angle, ortho, grid and the rest), with Draft's snap palette on the AxisFlow toolbar and an unconditional **axis hold** (while the cursor rides a compass axis, a snap contributes its along-axis coordinate instead of pulling the point off the axis); a live **settings dialog** (sections Compass / Live Line / Behaviour / Hotkeys) that applies every option on the fly, including rebinding keys by pressing them; editable default keys in **`hotkeys.ini`**; **interface translation** following FreeCAD's UI language (eight languages bundled); palette commands with icons and a workbench icon; automated tests (C++ unit tests plus a Python API suite); and the optional Draft adapter.

Not there yet (**not yet implemented**): typed **absolute/relative** entry beyond the per-axis world conversion; a dockable coordinate panel; Sketcher support; a published **Linux/macOS build** (only Windows is built today — the cross-platform build is in progress).

Not working yet: **unit suffixes in the coordinate fields.** The fields accept
digits, a sign and a decimal separator only, so `1,5 m` or `2 ft` cannot be
typed -- values are entered in the document's base unit. The parser behind them
(`Base::Quantity`) already understands units; re-allowing letters in the field
validator is what is missing.

## Panel theme

The floating panel follows the FreeCAD theme that is current when the panel
is shown and when FreeCAD changes the theme while the panel is open. AxisFlow
does not have to be restarted for a theme that FreeCAD applies immediately.
Background, labels and fields use Qt's palette roles for the theme that is
already loaded. AxisFlow does not guess a theme by name and does not recolor
the rest of FreeCAD.

**Active field colour** (Compass section of the AxisFlow settings) still
overrides the active field. The factory value is automatic: the field uses a
theme highlight and a text colour chosen to stay readable, and the selection
stays visible. A custom colour is drawn over the theme field background,
including its transparency, and the text colour is chosen for contrast. The
saved value is not rewritten. The factory value and an explicitly saved copy
of that same value are treated the same, because they are the same number; the
settings swatch can still show the old grey while the panel uses the automatic
highlight.

Lock checkboxes, the Local/World button and the close button keep the normal
Qt / FreeCAD style. The close icon is taken again from that style when the
theme is applied. Typing, locks, the coordinate frame and shortcuts are
unchanged.

The chord hint uses the panel background and a readable text colour from that
same pair. It is not taken from the tooltip palette. The hint stays a separate
window and does not take focus.

When a theme restyle touches a coordinate field, the code keeps the selected
range and which end holds the caret. A selection made from right to left is
restored with a negative length in `QLineEdit::setSelection`, and only if the
style change actually moved the range or the caret. That path has not been
run in FreeCAD.

A third-party stylesheet that paints controls without putting those colours
into Qt palette roles can leave the panel on the ordinary window colour.
AxisFlow does not parse stylesheet files. This source change is not visible
until a binary built against the same FreeCAD is installed. That build has not
been run, and the panel contrast issue is not confirmed fixed.

## Object snapping

AxisFlow does not implement its own snapping: inside its tools it asks FreeCAD's `Snapper`, so
endpoint, midpoint, centre, intersection, perpendicular, near, extension, parallel, angle, ortho,
grid and the rest behave exactly as they do in Draft and BIM, draw the same markers, and share the
same on/off state. Draft's snap commands are added to the **AxisFlow snapping** toolbar, so the
familiar palette is available without leaving the workbench. Turn the whole thing off with the
*Use object snapping* option.

A snapped point is an explicit target, so it outranks the soft guides: while a snap is live both
the candidate point and the candidate axis of live-line indexing stand down, and the snapped point
becomes the point being placed. It is **not** flattened onto the compass plane — the X/Y/Z fields
show its true 3-D coordinates, so snapping to geometry above or below the plane reports the real
height. A locked field still wins over the snap: a frozen Z, for example, pins the snapped point to that height in compass space.

**Smart lock** changes what happens when the cursor was hugging an axis and a snap fires. Normally
the candidate axis disappears; with Smart lock on it survives and freezes at the perpendicular
projection of the snapped point onto that axis. That is the quick way to take a distance from
existing geometry while staying exactly on the axis. Which axis is chosen follows the *cursor*, not
the snapped point, since the snap itself may be far off to one side.

The full precedence, later winning: raw cursor → object snap → indexing → locked field → typed value.

## Keys and their configuration

AxisFlow is keyboard-first. Inside its own tools the caret always sits in the active coordinate field, so digits edit the field while bound letters act as commands. Every binding is a **Qt key code → action** map with sensible defaults, stored in FreeCAD's parameter tree and reconfigurable at runtime.

### Default keys (inside AxisFlow's own tools)

**Global actions** — resolved wherever the session has focus (the coordinate window or the 3-D view). Combinations with Ctrl / Alt / Meta are never claimed and pass through to FreeCAD.

| Key | Action | What it does |
|---|---|---|
| `Space` | ToggleMode | switch ortho ↔ radial (rectangular ↔ polar) |
| `P` | ConfirmPoint | place the point; commits the typed coordinates |
| `Enter`, numpad `Enter` | SmartLock | constrain movement to the axis the cursor leans towards; press again to release |
| `X` | LockX | **freeze the X field** at its current value (the checkbox next to it ticks; the other fields keep following the cursor); press again to release. Ortho mode only |
| `Y` | LockY | freeze the Y field the same way. Ortho mode only |
| `Z` | LockZ | freeze the Z field; with an object snap live this pins the snapped point's height in compass space. Works in both modes |
| `D` | LockD | freeze the distance field. Radial mode only |
| `O` | SetOrigin | move the compass origin to the cursor (snap included) **without placing a point** — the running command ignores it |
| `T` | RotateTop | align the compass with the global axes |
| `F` | RotateFront | front orientation: compass Z along global −Y, compass Y along global +Z |
| `S` | RotateSide | side orientation: compass Z along global +X, compass Y along global +Z |
| `V` | RotateByView | align the compass with the current camera orientation |
| `E` | RotateCycle | cycle Top → Front → Side → Top… |
| `M` | ToggleSegmentMode | relayed to the running command; the line demo switches the next segment between a straight line and an arc |
| `Escape` | FinishLine | finish the **current** line (it is saved) and start a new one; the compass and coordinate window stay up |
| `` ` `` (backtick) | CancelCommand | finish the current line **and** end the whole command |
| `R` then `X`/`Y`/`Z` | RotateAboutX/Y/Z | turn the compass 90° about its own axis (a two-key chord — see below) |

**Coordinate-window editing keys** — active while a field has the caret (own-tool mode):

| Key | What it does |
|---|---|
| `0`–`9`, `.`, `,`, `-`, `+` | start / continue typing a value into the active field |
| `Backspace` | delete the last character of the entry — **only while a value is being typed**; otherwise the key goes to FreeCAD |
| `Delete` | clear the entry and release that field's override — again **only while typing**, so `Del` still deletes the selected object the rest of the time |
| `Tab` / `Shift`+`Tab` | move the active field to the next / previous visible field |
| the global keys above | still act as actions even while a field is focused |

Note on the coordinate-frame button: the button below the fields is **not** a mode toggle. It switches the read-out (and typed entry) between **Local** — offset from the compass origin along the compass axes — and **World** — the same axes measured from the world origin. Ortho/radial stays on `Space`, shown by the field labels (`X`/`Y`/`Z` vs `D`/`A`).

Entry behaviour (AccuDraw-style): the first character typed replaces the field's live read-out rather than appending to it. A bare magnitude takes the **sign of the cursor's current direction** along that axis — point down −X, type `5`, and X locks at −5 (the live line follows immediately); an explicitly typed sign always wins. On focus loss a committed value is rounded to the standard precision (4 decimals; 2 for the polar angle) and normalised (`5cm` → `50.0000`).

The lock checkboxes next to the fields carry the same meaning: **ticked = the field is frozen** and stops changing as the mouse moves — whether it was frozen by a lock key, by the checkbox itself, or by typing a value. Unticking releases the field back to the live read-out. All locks are per-point: placing a point clears them.

`SmartLock` (default `Enter`) constrains movement to the axis the cursor leans towards by freezing the **other two** fields at 0 — X+Z frozen means movement only along Y, Y+Z frozen means movement only along X; in radial mode it freezes the angle at the nearest 0/90/180/270° and Z at 0, leaving the distance free; press again to release.

Actions with **no default key** but available through the API and `hotkeys.ini`: `ToggleActive` (turn AxisFlow on/off — it works in **both** directions, including while AxisFlow is off), `ClearLocks` (release every locked field), `CancelEntry` (layered: abort a value entry, then release locks). The settings dialog shows the everyday keys on the **Control hotkeys** page and the orientation keys on **View hotkeys**; `ClearLocks`, `CancelEntry` and `ConfirmPoint` are bindable through `hotkeys.ini` and the API but are not listed in the dialog.

### Changing the defaults (`hotkeys.ini`)

The **default** layout lives in an editable `hotkeys.ini` in the Mod folder, so changing it needs **no rebuild** — edit and restart FreeCAD:

```ini
[hotkeys]
ConfirmPoint  = P
SmartLock     = Return, Enter   ; several keys may share one action
FinishLine    = Escape          ; comments with ; or #
RotateCycle   = E
ToggleActive  =                 ; empty = not assigned
```

Keys are written by Qt name (`Insert`, `Space`, `Return`, `Enter` for the numpad, `Escape`, `F5`, `X`, `` ` ``) or as a raw code (`0x1000005`). A typo is reported in the Report view and that binding is skipped, never silently mis-bound.

Three layers apply in order, later winning: the built-in layout compiled into the module (a fallback, so a missing or damaged file can never stop the workbench from starting) → `hotkeys.ini` → keys the user rebound themselves. Editing the file therefore changes what a *new* user gets without overriding a deliberate personal choice; to adopt it yourself after rebinding, run `AxisFlow.resetSettings()`. `AxisFlow.loadHotkeyDefaults(path)` re-reads the file without restarting.

### Reconfiguring keys

The **Settings** dialog has a **Hotkeys** section: click a key field, press the key you want, and it is assigned and saved immediately. A ✕ button unassigns an action, and *Restore default keys* drops your overrides and falls back to the defaults layer (built-in plus `hotkeys.ini`) without touching the other options. If the key you press already belongs to another action, AxisFlow asks before taking it away. Only plain keys are accepted — combinations with Ctrl/Alt/Meta are refused, because AxisFlow never claims those (they stay with FreeCAD).

From Python:

Per-user bindings live under `User parameter:BaseApp/Preferences/Mod/AxisFlow` as `Hotkey.<Action>` entries: a comma-separated list of Qt key codes (an empty string means "unbound"). Change them from Python:

```python
import AxisFlow as AF
from PySide import QtCore

# Rebind "finish line" from Escape to F2 (persisted to settings):
AF.setHotkey(int(QtCore.Qt.Key_F2), 'finishline')

# Unbind a key (e.g. free Space so it does nothing):
AF.setHotkey(int(QtCore.Qt.Key_Space), 'none')

# Session-scoped change that must NOT touch the stored layout (persist=False):
AF.setHotkey(int(QtCore.Qt.Key_X), 'none', False)

# Inspect the live map, and restore the built-in layout:
AF.getSettings()['hotkeys']       # -> {'FinishLine': [16777216], 'LockX': [88], ...}
AF.loadHotkeysFromSettings()      # re-apply defaults + stored overrides
AF.resetSettings()                # restore ALL defaults (options + hotkeys)
```

Action names accepted by `setHotkey` (case-insensitive-friendly lowercase): `toggleactive`, `togglemode`, `smartlock`, `lockx`, `locky`, `lockz`, `lockd`, `clearlocks`, `cancelentry`, `cancelcommand`, `finishline`, `confirmpoint`, `none`.

An action may hold several keys (e.g. `ConfirmPoint` is bound to both `Return` and the numpad `Enter`); `getSettings()['hotkeys']` lists each action's full key list.

The optional Draft adapter deliberately unbinds `X`/`Y`/`Z`/`Enter`/`Escape` for the duration of its session (with `persist=False`, so your stored layout is untouched) because those belong to Draft while its command runs.


### Two-key chords

A binding can be a **pair of keys pressed in sequence**, the way MicroStation
does it: press the prefix, then the key that completes it. Three ship by
default and turn the compass 90 degrees about its *own* axis, wherever it
currently points (unlike Top/Front/Side, which are absolute):

| Keys | Action |
|---|---|
| `R` `X` | turn 90 deg about the compass X axis |
| `R` `Y` | turn 90 deg about the compass Y axis |
| `R` `Z` | turn 90 deg about the compass Z axis |

While the prefix is waiting, a small hint appears beside the coordinate window
listing what is on offer, one option per line. Any key that does not complete a
chord cancels the wait and does nothing else, so a mistyped `R`, `Y` never
silently freezes the Y field; Escape cancels it too. There is no timeout.

**A key is either a binding of its own or the first key of a chord, never
both.** With `R X` defined, plain `R` cannot be assigned to anything --
otherwise pressing `R` could only be resolved by waiting to see whether a
second key follows, and behaviour would depend on how fast you type. For the
same reason a chord is limited to two keys. Bindings that break the rule are
refused, with an explanation, wherever they are made.

To record a chord in the settings dialog, hold **Shift** while pressing the
first key; the button then waits for the second. In `hotkeys.ini` a space joins
the two keys of a chord, while a comma still separates alternative keys:

```ini
SmartLock    = Return, Enter   ; two alternative single keys
RotateAboutX = R X             ; one chord
```

## Settings

All options are stored in FreeCAD's own parameter tree under `User parameter:BaseApp/Preferences/Mod/AxisFlow`. That is an XML file (`user.cfg`) in the user's config directory: always writable, surviving a module reinstall, readable identically from C++ and Python. Every option is also reachable from Python via `AF.getSettings()` / `AF.setSetting(name, value)`.

The **Settings** command (`AxisFlow_Settings`, gear icon) opens a live, non-modal editor. Options are grouped into sections listed on the left (**Compass** / **Live Line** / **Behaviour** / **Hotkeys**), each a two-column grid of name → value where the value widget matches the option's type (check box, combo, number spin, colour picker). Every change applies **on the fly** — there is no OK button — so the compass and panel update as you edit. It is a thin Python/PySide dialog (`axisflow_settings.py`); adding a future option is one row in the relevant section of `_SECTIONS` plus a getter/setter in `AxisFlowSettings`. The interface language follows FreeCAD's (see the **Language** section).

| Key | `setSetting` name | Type | Default | Meaning |
|---|---|---|---|---|
| `CompassSize` | `compassSize` | Int | 100 | compass size, pixels |
| `AxisThickness` | `axisThickness` | Int | 4 | axis line thickness, pixels |
| `AxisStartPct` | `axisStartPct` | Float | 0.80 | where an axis starts, as a fraction of the centre-to-contour distance |
| `AxisLengthPct` | `axisLengthPct` | Float | 0.35 | axis length, same fraction |
| `FillTransparency` | `fillTransparency` | Float | 0.65 | transparency of the compass fill (0..1) |
| `AxisColorX` | `axisColorX` | Unsigned | `0xFF0000FF` | X axis colour (`0xRRGGBBAA`), red by default |
| `AxisColorY` | `axisColorY` | Unsigned | `0x00FF00FF` | Y axis colour, green by default |
| `ActiveFieldColor` | `activeFieldColor` | Unsigned | `0xDCDCDCFF` | background of the active coordinate field |
| `AlwaysShowCompass` | `alwaysShowCompass` | Bool | false | show the compass whenever loaded, not only during a command |
| `LiveLineEnabled` | `liveLineEnabled` | Bool | false | draw the live guide line from the compass centre to the cursor |
| `LiveLineStyle` | `liveLineStyle` | Int | 0 | 0 = solid, 1 = dashed, 2 = dotted, 3 = dash-dot |
| `LiveLineColor` | `liveLineColor` | Unsigned | `0x00AAFFFF` | live guide line colour (`0xRRGGBBAA`) |
| `LiveLineTransparency` | `liveLineTransparency` | Float | 0.50 | transparency of the live guide line (0..1) |
| `CompassPathEnabled` | `compassPathEnabled` | Bool | true | draw the "compass path": a thin poly-line from the compass centre through the prospective point to the snap, broken into a leg per axis. Shown while a snap is live or any field is frozen |
| `CompassPathStyle` | `compassPathStyle` | Int | 1 | 0 = solid, 1 = dashed, 2 = dotted, 3 = dash-dot |
| `CompassPathColor` | `compassPathColor` | Unsigned | `0xC8C8C8FF` | compass path colour (`0xRRGGBBAA`), light grey by default |
| `UseDistanceIndexing` | `useDistanceIndexing` | Bool | true | snap the live-line length to the last-entered "indexed distance" |
| `UseAxesIndexing` | `useAxesIndexing` | Bool | true | snap the live line onto the nearest X/Y axis |
| `IndexingTolerance` | `indexingTolerance` | Int | 15 | snap tolerance, pixels (0..50) |
| `IndexingAxisWeight` | `indexingAxisWeight` | Int | 8 | candidate-axis line thickness, pixels |
| `IndexingAxisTransparency` | `indexingAxisTransparency` | Float | 0.50 | transparency of the candidate-axis line (0..1) |
| `CompassAnimation` | `compassAnimation` | Bool | true | animate the compass at all. Off restores the pre-animation behaviour exactly — the compass jumps to its new plane and anchor with no easing, no cross-fade and no timer running. Worth turning off on a slow machine: the fade costs frames, and drawing a polyline re-anchors the compass after every point |
| `CompassRotateAnimation` | `compassRotateAnimation` | Float | 0.5 | how long that animation takes, in seconds (0 = instant, clamped to 0–2; ignored when `CompassAnimation` is off). Drives both movements: a plane change sweeps, and a change of anchor cross-fades — the compass dissolves out of the old position and into the new one at the same time. Display only: the solver switches plane and origin immediately, the compass catches up |
| `CompassFollowDirection` | `compassFollowDirection` | Int | 0 | after a confirmed point, align the compass +X with the segment just drawn: 0 = off, 1 = on keeping Z (in-plane rotation), 2 = on fully (the normal stays as close to the old one as possible) |
| `FocusAdvanceOnMove` | `focusAdvanceOnMove` | Bool | true | after typing a value into an in-plane field, a deliberate cursor move (>6 px) finishes the entry and moves the caret to the adjacent field (X <-> Y, D <-> A) |
| `SmartNegativeDirection` | `smartNegativeDirection` | Bool | true | a bare magnitude typed into a local field takes the sign of the cursor's direction along that axis; off = the typed sign is literal |
| `UseSnapping` | `useSnapping` | Bool | true | use FreeCAD (Draft) object snapping inside AxisFlow tools |
| `Hotkey.<Action>` | — | ASCII | see above | comma-separated Qt key codes; empty = unbound |

Actions used as `Hotkey.<Action>` keys: `ToggleActive`, `ToggleMode`, `SmartLock`, `LockX`, `LockY`, `LockZ`, `LockD`, `SetOrigin`, `RotateTop`, `RotateFront`, `RotateSide`, `RotateByView`, `RotateCycle`, `RotateAboutX`, `RotateAboutY`, `RotateAboutZ`, `ToggleSegmentMode`, `ClearLocks`, `CancelEntry`, `CancelCommand`, `FinishLine`, `ConfirmPoint`.

In a value, a comma separates **alternative** bindings and a space joins the two keys of a **chord**: `"16777220,16777221"` is Return plus numpad Enter, `"82 88"` is the chord `R X`. A key may be either a single binding or the first key of a chord, never both — otherwise pressing the prefix could not be resolved without a timeout.

```python
import AxisFlow as AF
AF.setSetting('liveLineEnabled', False)   # hide the live guide line
AF.setSetting('liveLineStyle', 0)         # solid
AF.setSetting('liveLineColor', 0x33CCFFFF)# light blue
AF.setSetting('compassSize', 160)
AF.getSettings()                          # -> dict, including the 'hotkeys' map
AF.resetSettings()                        # restore built-in defaults
```

Each section has its own **Restore defaults** button, which writes back the factory values of that section alone — adjusting the compass will not touch your indexing tolerances or your keys. The two hotkey pages have a key-reset button of their own (it restores the keys of both pages).

## Commands

The workbench menu / toolbar exposes:

* **Activate AxisFlow** (`AxisFlow_Activate`) — toggle the standalone compass tool on the active 3-D view.
* **Draw line** (`AxisFlow_DrawLine`) — draw a polyline through AxisFlow's `getPoint()` API (Escape starts a new line, backtick ends the command). The geometry lives in the demo consumer, `axisflow_demo.py`. Press **M** to make the next segment an arc: click its endpoint, then a point along the arc. Arcs are not forced to be tangent, so segments may meet at any angle, and they are not restricted to a plane. The line can be snapped to itself while you draw it: its confirmed part is published as a temporary object, so endpoints, midpoints and perpendiculars of the segments you have already placed are ordinary snap targets.
* **Move** (`AxisFlow_Move`) — move the selected objects: pick a base point, then a target. Because the compass anchors on the base point, the second point is usually *typed* as a displacement (`X`, `150`, Enter). With nothing selected, the first click picks the object under the cursor **and** serves as the base point.
* **Copy** (`AxisFlow_Copy`) — the same, leaving the originals in place.

  With nothing selected, all three transform commands open in **pick mode**: the compass steps aside — there is nothing to aim yet — and the cursor becomes an arrow with a target tail. Click an object and the compass returns; from there it is ordinary AxisFlow input. For Move and Copy that click is still the base point too. The coordinate panel stays up throughout, and a compass pinned on screen with `AlwaysShowCompass` stays visible during the pick.
* **Extrude** (`AxisFlow_Extrude`) — sweep a planar wire into a surface. Pick the wire and the compass stands up along its normal, so the compass **X axis is the sweep**: the height is typed (`X`, `150`, Enter) rather than hunted for with the mouse. The surface grows live while you drag. Letting the cursor wander off the axis gives an *oblique* extrusion, which is allowed on purpose — press `X` or SmartLock when you want it square. Pick the wire anywhere along its length and the compass anchors there, not at whichever end is vertex zero. Escape cancels. The result is a parametric `Part::Extrusion` with `Solid = False`, so the height stays editable in the property view afterwards. Geometry lives in `axisflow_extrude.py`.
* **Settings** (`AxisFlow_Settings`) — open the live settings dialog described above.

`AxisFlow_ToggleLiveLine` (show / hide the live guide line) is still registered but no longer sits on the palette — the live line is a checkbox in the settings dialog. Add it to a custom toolbar, or call it from a macro, if you want it back as a button.

## Installation

AxisFlow is a **compiled C++ workbench**, so it is not installed through FreeCAD's
Addon Manager (which only handles Python-only addons). Install a prebuilt release
that matches your FreeCAD.

### Windows

1. Download `AxisFlow-<version>-win64-FC1.1.zip` from the project's **Releases** page
   (or run the `setup.exe` installer, which places the files and registers an
   uninstaller — see `man/RELEASE.md`).
2. Unzip it so the `AxisFlow` folder lands in your FreeCAD `Mod` directory — on
   Windows `%APPDATA%\FreeCAD\Mod\`, giving `...\FreeCAD\Mod\AxisFlow\InitGui.py`.
   (The exact path is printed by `App.getUserAppDataDir()` in FreeCAD's Python console.)
3. Fully restart FreeCAD and pick **AxisFlow** from the workbench selector.

### Linux (one command) — *not yet implemented*

> **Not available yet.** No Linux build is published (this needs the
> cross-platform build, still in progress), so the command below has nothing to
> download for now. The installer script itself is finished and will work once a
> Linux release exists. Windows is the only supported platform for the moment.

For FreeCAD **1.1.x on x86_64**, the planned one-line install is:

```bash
curl -fsSL https://raw.githubusercontent.com/LeonidWiebe/AxisFlow/main/install.sh | bash
```

The script detects your FreeCAD version and Mod folder, downloads the prebuilt
tarball from Releases, verifies its checksum and installs it. If your FreeCAD is
**not** 1.1.x it stops rather than install an incompatible binary — pass `--force`
to override, `--mod-dir=…` to choose the target, or `--uninstall` to remove it.
Prefer to inspect first? `curl -fsSLO …/install.sh && less install.sh && bash install.sh`.

A compiled module only loads in a FreeCAD build whose **Python/Qt/Coin ABI matches**
the one it was built against, so use a release built for your FreeCAD. If the
workbench doesn't appear after a restart, the binary likely doesn't match your build.
Maintainers: see `man/RELEASE.md` for how to package and publish a release.

## Language

AxisFlow's interface follows **FreeCAD's own language** (Preferences → General →
Language) — set FreeCAD's language and AxisFlow matches it, including on first
start. There is no separate language switch (this is FreeCAD's policy for
add-ons). Translations are shipped as compiled `.qm` files in `translations/`.
Bundled languages: German (`de`), French (`fr`), Hungarian (`hu`), Chinese
Simplified (`zh-CN`), Korean (`ko`), Hindi (`hi`), Serbian (`sr`) and Russian (`ru`) — these are
initial, machine-assisted translations and welcome review by native speakers. To
add or improve a translation, see `man/TRANSLATING.md`; it uses Qt Linguist
`.ts`/`.qm` files, so anyone can contribute one.

## Build (Windows)

Reference environment: Windows, MSVC (Visual Studio 17 2022, x64), FreeCAD 1.1.1 with Qt 6 and Coin 4 from a **pixi** environment. See `man/BUILD_WINDOWS.md` for setup and `man/HANDOFF.md` for the CMake pinning and the build pitfalls already solved.

```bat
cd C:\projects\AxisFlow
rmdir /s /q build
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config RelWithDebInfo
cmake --install build
REM the .py glue and Resources are installed too; the Mod path is queried from FreeCAD
```

The module installs into FreeCAD's own user Mod directory (`%APPDATA%\FreeCAD\Mod\AxisFlow\`, queried from FreeCAD rather than hard-coded) and builds as `AxisFlow.pyd`. Adding or renaming sources requires a clean reconfigure — delete `build` first. Pure-Python files (`axisflow_demo.py`, `axisflow_snap.py`, `axisflow_snapping.py`, `axisflow_settings.py`) and the data files (`hotkeys.ini`, `translations/*.qm`) can be dropped into the Mod directory without rebuilding.

## Testing

Automated tests come in two tiers (details in `man/TESTING.md`):

* **Tier 1 — C++ unit tests** (solver + hotkeys + entry policy): pure logic, no GUI. Build with `-DAXISFLOW_BUILD_TESTS=ON` and run `ctest`, or just run `run_tests.bat`.
* **Tier 2 — Python API tests** (`TestAxisFlow.py`): run inside a GUI FreeCAD with `import Test, TestAxisFlow; Test.runTestsFromModule(TestAxisFlow)`.

## Python API

```python
import AxisFlow as AF

AF.isActive()                     # -> bool
AF.activate() / AF.deactivate()
AF.beginSession() / AF.endSession()   # raise / tear down compass + panel

AF.setPlane(u, v, normal)         # orient the compass (App.Vector each). Normalised but NOT
                                  # orthogonalised -- pass a known-orthonormal triple
AF.alignCompassX(direction)       # safer: aim the compass X axis at a direction you computed,
                                  # v and the normal are derived orthonormal from it
AF.resetPlane()
AF.setCompassVisible(False)       # hide the COMPASS ONLY -- panel stays up, service keeps
                                  # running. Display state, not activation: deactivate()
                                  # would also hide the panel and be undone by the next
                                  # getPoint(). Ignored while AlwaysShowCompass is on,
                                  # and cleared automatically when the session ends
AF.setMode('rect')                # 'rect' (ortho) | 'polar' (radial)
AF.getMode()

AF.lockAxis('x')                  # freeze that field at its current value;
                                  # 'x' | 'y' | 'z' | 'd' | 'a' | 'none' (= release all)
AF.clearLocks()                   # release every locked field (typed values survive)
AF.setKeyValue(0, 1500.0)         # rect: 0=X 1=Y 2=Z ; polar: 0=dist 1=angle 2=Z
AF.clearKeyValues()
AF.setDisplayLock('x')            # show a lock WITHOUT taking one: the Draft adapter mirrors
                                  # Draft's own constraint mask so the compass reads right
                                  # while Draft, not AxisFlow, owns the constraint
AF.setIndexedDistance(1500.0)     # the length the live line snaps back to
AF.clearIndexedDistance()

AF.setBasePoint(pt)               # move the compass anchor WITHOUT confirming a point
AF.getCursorPixel()               # -> (x, y), the cursor in viewport pixels
AF.getCompassInfo()               # -> dict: origin and planeU / planeV / planeN as TUPLES
                                  # (not Vectors), plus radius, visible, active, mode,
                                  # hasSession, hasCompass -- what is actually on screen

AF.solve(raw[, last])             # -> Vector, constrain without touching the UI
AF.updateCursor(raw[, last])      # same, plus move the compass / panel
AF.confirm(pt)                    # commit a point as the new origin
AF.getState()                     # -> dict

AF.setConstrainEnabled(bool)      # off only in the Draft adapter
AF.constrainEnabled()             # -> bool
AF.setHotkey(0x58, 'lockx')       # persist a binding; add False to keep it session-scoped
AF.setHotkeys({'lockx': '88'})    # several at once
AF.loadHotkeysFromSettings()
AF.loadHotkeyDefaults(path)       # read the default layout from an INI (hotkeys.ini) and
                                  # re-apply built-in -> file -> user, in that order
AF.resetHotkeys()                 # forget the user's overrides and fall back to the
                                  # defaults (built-in plus hotkeys.ini); other settings
                                  # untouched

AF.getSettings()                  # -> dict, including the 'hotkeys' map
AF.getDefaultSettings()           # -> dict, the built-in values
AF.setSetting('compassSize', 160)
AF.resetSettings()
```

### getPoint — the consumer entry point

```python
AF.getPoint(callback=None, movecallback=None, last=None,
            continuous=False, finishcallback=None)
```

* `callback(point, ok)` — a point was confirmed (`ok=True`, `point` is a `Base.Vector`) or the command was cancelled (`ok=False`, `point=None`).
* `movecallback(point)` — the live solved point on every cursor move; draw your own preview here.
* `finishcallback()` — the user pressed FinishLine (Escape): finish the current line and keep drawing; the session stays up.
* `last` — an optional base point (relative-input anchor).
* `continuous` — `False` fires `callback` once, then finishes; `True` fires per point until cancel.

**Working plane.** AxisFlow constrains points to the plane through the anchor with the compass orientation. A consumer that wants to draw on FreeCAD's active working plane should sync it once at command start:

```python
import WorkingPlane, FreeCAD as App
wp = WorkingPlane.get_working_plane()
AF.setPlane(App.Vector(wp.u), App.Vector(wp.v), App.Vector(wp.axis))
```

`axisflow_demo.py` does exactly this. A minimal multi-line consumer:

```python
import AxisFlow as AF, FreeCAD as App, Draft

class Line:
    def __init__(self): self.pts = []
    def on_point(self, p, ok):
        if not ok:                       # backtick: end command
            self.finish(); AF.endSession(); return
        self.pts.append(App.Vector(p))
    def on_finish(self):                 # Escape: save this line, keep going
        self.finish(); self.pts = []
    def finish(self):
        if len(self.pts) >= 2:
            Draft.make_wire(self.pts, closed=False)
            App.ActiveDocument.recompute()

d = Line()
AF.getPoint(callback=d.on_point, finishcallback=d.on_finish, continuous=True)
```

## Optional Draft adapter

```python
import axisflow_snap
axisflow_snap.install()      # compass over a running Draft/BIM command
axisflow_snap.uninstall()
```

The adapter constrains nothing and binds no keys. It mirrors Draft's own restriction in the panel checkboxes (`setDisplayLock`; Draft's "move along X" shows as frozen Y and Z), extends Draft's typed-value hold while you type, and unbinds AxisFlow's X/Y/Z/Enter/Escape for the duration of the session so Draft's native shortcuts keep working.

## Architecture

```
src/
  AppAxisFlow.cpp            module entry; attaches the Python method table
  Workbench.{h,cpp}          thin workbench (menu + toolbar)
  Command.cpp                Activate / Draw line / Toggle live line / Settings
  AxisFlowSolver.{h,cpp}     pure math: plane, origin, fixed-slot locks, key overrides
  AxisFlowService.{h,cpp}    singleton brain: state, signals, hotkeys, actions
  AxisFlowSettings.{h,cpp}   persistent options on FreeCAD's ParameterGrp
  AxisFlowHotkeys.{h,cpp}    reconfigurable Qt-key -> action map
  AxisFlowEntry.h            pure entry policy (sign-from-direction); unit-tested
  AxisFlowEventFilter.{h,cpp}  global key filter (app-wide + adapter)
  AxisFlowCompass.{h,cpp}    Coin3D scene graph (compass + live guide line)
  AxisFlowPanel.{h,cpp}      floating Qt panel (title/power, read-out, locks, Local/World)
  AxisFlowTool.{h,cpp}       per-view coordinator (standalone / adapter)
  AxisFlowGetPoint.{h,cpp}   getPoint() bridge: Python callbacks <- state machine
  AxisFlowPy.cpp             Python bindings (module `AxisFlow`)
tests/                       C++ unit tests (GoogleTest): solver, hotkeys, entry
axisflow_snap.py             optional Draft adapter (compass over Draft/BIM tools)
axisflow_snapping.py         bridge to FreeCAD's Snapper (object snapping + snap palette)
axisflow_demo.py             demo Line command (reference consumer, outside core)
axisflow_settings.py         live settings dialog (Python/PySide)
TestAxisFlow.py              Python API tests (run in a GUI FreeCAD)
hotkeys.ini                  editable default key layout (no rebuild needed)
translations/                Qt Linguist sources and compiled .qm (8 languages)
Resources/icons/             command and workbench icons
installer/                   NSIS script for the Windows setup.exe
man/                         documentation (CHANGELOG, HANDOFF, TESTING, ...)
```

Only one input session may be active at a time — the service is a singleton by deliberate choice. Session teardown is deferred (`deleteLater`) so it is safe to end a command from inside the very event handler that owns the tool.

## License

AxisFlow is licensed under the **GNU Lesser General Public License v2.1 or later**
(LGPL-2.1-or-later); the full text is in the `LICENSE` file.

Early alpha (0.1.x). Interfaces and behaviour are still changing. Contributions and issue reports are welcome.
