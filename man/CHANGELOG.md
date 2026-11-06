# Changelog

All notable changes to **AxisFlow** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is in `0.x` (alpha), the public API may still change between
minor releases.

## [Unreleased]

### Changed
- The floating coordinate panel follows the current FreeCAD theme, including a
  theme switch while the panel exists. Colours come from Qt palette roles of
  the theme that is already loaded; no theme name is guessed and the rest of
  FreeCAD is not recolored.
- The factory active-field colour is now an automatic theme highlight with a
  matched text colour and a visible text selection. A custom Active field
  colour is composited over the theme field, including its alpha, and keeps a
  readable foreground. The saved setting is not rewritten. An explicitly saved
  copy of the factory value is treated as automatic, because it is the same
  value.
- Coordinate input, locks and shortcuts are unchanged. The panel has not been
  built or installed, so a running FreeCAD does not show this until a
  compatible binary is produced.
- The chord hint uses the panel background and a readable foreground from that
  pair. It does not read the tooltip palette.
- Restyling a field keeps the selection range and the caret end. A right-to-left
  selection is restored with a negative `QLineEdit::setSelection` length, and
  only when the style change moved the range or the caret. This has not been
  checked in the GUI.

## [0.2.1] - 2026-07-27

### Added
- **Extrude command** (`AxisFlow_Extrude`, `axisflow_extrude.py`): sweep a planar
  wire into a surface. Picking the wire stands the compass up along its normal,
  so the compass **X axis is the sweep** and the height is typed (`X`, `150`,
  Enter) rather than dragged for. The surface grows live while the cursor moves.
  Letting the cursor wander off the axis gives an oblique extrusion on purpose --
  `X` or SmartLock is how you ask for square. The result is a parametric
  `Part::Extrusion` with `Solid = False`, direction in `Dir` and height in
  `LengthFwd`, so the height stays a single editable number afterwards. The core
  still creates nothing: this is an ordinary `getPoint()` consumer.
- `AxisFlow.alignCompassX(direction)`: aim the compass X axis at a direction and
  derive the other two axes orthonormal from it. The solver had carried
  `alignCompassX()` since the beginning but never called or exposed it. It is the
  safe counterpart to `setPlane()`, which normalises its three vectors but does
  **not** orthogonalise them -- and the solver inverts the compass matrix by
  transposing it, so a skewed triple silently yields a skewed coordinate system.
- `TransformTool` gained `pick_consumes_point`, `modules`, `on_objects_ready()`
  and `on_end()`. Move and Copy keep spending the picking click on the base point;
  a tool that must inspect the object before it can accept a point (Extrude has to
  know the wire before it can aim the compass) now opts out instead of forking the
  machine.
- **Move animation for the compass.** Re-anchoring used to be an instant jump.
  It now cross-fades: a ghost frozen at the old anchor fades out while the live
  compass fades in at the new one, simultaneously, so the compass reads as
  travelling rather than teleporting. Timing comes from the existing
  `CompassRotateAnimation` setting, which now drives both animations (one timer,
  two independent clocks — they overlap whenever a confirm both re-anchors and
  re-aims). The ghost is a deep copy of the compass body taken when the move
  starts, so it needs no syncing; the live line, the path and the indexing
  markers stay out of it, since a ghost drawing a live line from a stale anchor
  to the current cursor would look like a bug.
  The body's `SoBaseColor` nodes became `SoMaterial` to make this possible:
  `SoBaseColor` has no transparency field. Under the compass's `BASE_COLOR`
  light model the rendered colour is the diffuse colour, so the output is
  unchanged. A per-subtree transparency override was the obvious alternative and
  is not available — in Coin the override bits for diffuse colour and
  transparency are deliberately the same bit (to match SGI/TGS Inventor), so
  forcing transparency would flatten the red and green axes to one colour.
- `CompassAnimation` (Bool, default true): turn the compass animation off
  entirely. Off restores the pre-animation behaviour exactly — the checkbox is
  folded into the duration, so every path in the compass takes the branch it
  already had for a zero duration: instant rotation, instant jump, no ghost, no
  deep copy, no timer scheduled. It exists because the fade costs frames while a
  line is being drawn, and a half-disabled animation would still pay them.
- **Pick mode** for Move, Copy and Extrude. While the command is waiting for an
  object the compass steps aside — there is nothing to aim yet and it is only in
  the way — and the cursor becomes an arrow with a target tail, MicroStation's
  "locate" made explicit. The instant the object is known the compass comes back
  and normal input begins. The coordinate panel stays up throughout.
  Cleared on every exit path, Escape mid-pick included, and the service clears it
  on deactivate() as well, so an abort cannot strand the user without a compass.
- Escape cancels **Extrude**. The key stays bound to `FinishLine` globally —
  for the line command that means "finish this polyline and keep drawing", which
  is worth more than uniformity — but Extrude overrides what finishing means: it
  has nothing partial worth keeping, so it ends and takes the preview with it.
  Backtick (`CancelCommand`) still works everywhere, and Move and Copy keep
  treating Escape as "start this one over".
- Extrude anchors the compass at the point of the wire **nearest where it was
  picked**, instead of at vertex zero, which on a long wall put the compass
  metres from where the user was looking. The 3D hit point now travels from
  `getObjectInfo` through `TransformTool.pick_point`, and is projected onto the
  wire with `distToShape` rather than used raw: the hit comes back through Coin
  in single precision and a wire is picked with a tolerance, so it lands near
  the wire rather than on it, and projecting also clamps to the wire's ends.
  A selection made before the command started was never clicked, so it still
  anchors at the first vertex.
- `AxisFlow.setCompassVisible(bool)`: hide or show the **compass only**, leaving
  the coordinate panel up and the service running. Display state, not activation.
  The first attempt at pick mode used `deactivate()` and silently did nothing,
  which is worth recording: `AxisFlowTool::activate()` calls
  `AxisFlowService::activate()` itself, so the `getPoint()` immediately after the
  hide switched the compass straight back on. `AlwaysShowCompass` outranks the
  hide — an explicit instruction beats a convenience.
- `tests/extrude_harness.py`: offline check of the sweep direction, the planarity
  refusal, the selection gate's lifetime, the point accounting, the emitted
  command string and the preview's quad bookkeeping -- 18 assertions, no FreeCAD.

### Fixed
- **Lines drawn flat with the compass were rejected as non-planar by Extrude.**
  Two causes, compounding. First, the real one: the cursor is unprojected onto
  the compass plane through Coin's single-precision types — `SbVec3f`, `SbLine`,
  `SbPlane`, with the plane's own origin and normal downcast from double on the
  way in. A float carries about seven significant digits, so at metre-scale
  coordinates the intersection landed ~5e-5 mm off the plane, and the solver
  passes that out-of-plane component straight through whenever Z is not locked.
  Every point of a polyline picked up its own offset, so the finished wire was
  not planar in the exact sense. The tool now snaps the unprojected point onto
  the plane in double precision, which brings the residual to ~1e-13 mm.
  Second: the check asked with OCC's default confusion of about 1e-7 — 500x
  tighter than the drift — so it could not have passed. The tolerance is now
  scaled to the wire, a millionth of its bounding-box diagonal (about a micron
  on a metre-long wire), which also rescues drawings made before the fix. The
  same tolerance goes to the straightness test on purpose: a dead-straight line
  carrying noise is not exactly straight, and a strict straightness test would
  drop it into `find_plane()`, which asked loosely would return a plane with an
  arbitrary normal — the "privileged directions" trap Draft warns about.
  Genuinely skew wires are still refused; an out-of-plane point from an object
  snap is still passed through rather than flattened.
- The installer can carry branding: `MUI_ICON`, the welcome/finish panel and the
  page header, for both the installer and the uninstaller. NSIS reads neither
  PNG nor anything else modern — per the Modern UI 2 reference the icon must be
  a real `.ico` and the page artwork 24-bit BMPs at 164x314 and 150x57 — so
  `installer/make_assets.py` converts the source PNGs, fitting rather than
  stretching them (almost no logo is shaped like a 164x314 strip) and flattening
  transparency onto a chosen background, since 32-bit BMPs with alpha render as
  a black box on some Windows versions. The generated files are committed and
  every reference is guarded with `!if /FileExists`, so a checkout without the
  artwork still builds, with the stock NSIS look, and `build_installer.bat`
  still needs nothing but makensis.
  The background is removed by flood-filling from the four corners rather than
  by a "white becomes transparent" threshold — the compass has a white dial face
  and white arrow tips, and the global rule would punch holes through them.
  Without it the icon would have installed as a plain white tile, since the
  source art is opaque RGB. The 150x57 header defaults to the bare mark instead
  of the full logo: a square logo with a tagline lands 57 px tall there, where
  the wordmark is a smear.
- The installer asked the user to close FreeCAD and took their word for it. It
  now checks, and checks the right thing: not whether FreeCAD is running, but
  whether the installed `AxisFlow.pyd` can be opened for writing. FreeCAD
  running *without* AxisFlow loaded installs fine — blocking on the process
  would refuse installs that would have succeeded — while a FreeCAD that has
  loaded it holds the module mapped as an image section and Windows denies
  write sharing, which is exactly what made `File` fail halfway and leave a
  half-written folder. The test is a plain `FileOpen` in append mode, so no
  third-party plugin (nsProcess, FindProcDLL) has to be dropped into every
  builder's NSIS. It runs on leaving the directory page and again just before
  the first file is written, and offers Retry rather than aborting. The
  uninstaller does the same: `RMDir /r` silently skips what it cannot delete,
  so it used to report success and leave the module behind.
- The NSIS build failed on `LicenseData: open failed "LICENSE"`. makensis
  changes the current directory to the one holding the `.nsi` unless told not
  to, so every relative path in the script resolved against `installer\`
  instead of the repo root — `LICENSE` first, and `dist\AxisFlow` and the
  output `setup.exe` immediately after it. `build_installer.bat` now passes
  `/NOCD`, and the assumption is spelled out in the `.nsi` header, which
  previously claimed the paths were relative to the repo root without saying
  what made them so.
- `stage_dist.bat` located the built `.pyd` with `for /r ... in (AxisFlow.pyd)`,
  which does not search: given a set with no wildcard, `for /r` simply prepends
  every directory under the tree to the name, existing or not. `PYD` therefore
  ended up as whichever directory the filesystem enumerated last — typically
  `build\ZERO_CHECK.dir\RelWithDebInfo\` — and the copy failed on a path that
  was never there. It had been working only by luck of enumeration order. The
  canonical `build\bin\<CONFIG>\` is now checked first, and the fallback search
  tests `if exist`. The Linux script was never affected: it uses `find`.
- The compass grew a hard dark ring the moment the move animation landed. Cause
  was the `SoBaseColor` → `SoMaterial` swap that made the fade possible:
  `SoBaseColor` sets only the diffuse colour, so the contour outline used to
  INHERIT the fill's transparency from the material above it, while `SoMaterial`
  brings its own `transparency` field defaulting to 0 — which reset the outline
  to fully opaque. The outline now carries the fill's colour and tracks its
  transparency, so it reads as part of the compass rather than a border around
  it. `kOutlineColor` is kept in the source for whoever wants the ring back.
- The animation tick rebuilt the guide line 60 times a second even when only the
  move fade was running, which cannot change it — the anchor reaches its final
  position before the first tick, and the fade only rewrites transparencies. It
  landed on the worst possible workload: drawing a polyline re-anchors the
  compass after every point, so a long line paid a rebuild storm per segment.
  The guide is now rebuilt only while the frame is actually turning.
- The NSIS installer shipped without `axisflow_transform.py`, so installer builds
  offered Move and Copy buttons whose commands died on `ModuleNotFoundError`. The
  duplicated file list is gone entirely: the script now ships the staged tree
  verbatim (`File /r`), which is what `stage_dist.bat` was already the single
  source of truth for. The same rot had silently dropped `CHANGELOG.md`, whose
  path had gone stale behind a `/nonfatal`.
- The resources directory was `resources/` on disk while CMake, `InitGui.py`,
  `package.xml` and the installer all spelled it `Resources/`. Invisible on
  Windows, broken on any case-sensitive filesystem -- i.e. exactly the Linux
  packaging path. Renamed, and the lowercase fallback in `stage_dist.sh` dropped.
- Three `viewer` dereferences in `AxisFlowTool` were unguarded while three others
  were checked. A live `QPointer` only proves the view's QObject survives, not its
  Coin side, and teardown is where they part company.
- `axisflow_snapping.py` documented the precedence chain with indexing above
  object snapping; the code has it the other way round, and deliberately so
  ("Object snap wins over indexing" in `AxisFlowIndexing.h`).

### Changed
- The settings table now lives **only** in README.md. The copy in the handoff
  notes had drifted on thirteen of twenty-one defaults and was missing five live
  options. `TestAxisFlow.test_readme_settings_table_matches_the_defaults` parses
  the README table and compares it against `getDefaultSettings()`, so a changed
  default or a new option cannot ship with the docs quietly disagreeing.

## [0.2.0] - 2026-07-24

### Added
- **Move and Copy commands** driven by the compass (`AxisFlow_Move`,
  `AxisFlow_Copy`). Pick a base point, then a target -- and because the compass
  anchors on the base point, the second one is normally *typed* as a
  displacement (press X, type 150, Enter) rather than hunted for with the
  mouse. With objects already selected they are used; with nothing selected the
  first click both picks the object under the cursor and serves as the base
  point, so one click does what Draft asks two for. Preview is a Draft ghost,
  the change is committed as a `Draft.move(...)` command string (undo stack and
  Python console both get it), and the commit is deferred out of the event
  callback. Implemented in `axisflow_transform.py` -- an ordinary `getPoint()`
  consumer, on a base class (`TransformTool`) sized for Rotate, Scale and Array
  to follow.
- `AxisFlow.getCursorPixel()` -- the cursor position in view pixels, so a
  consumer can ask `View.getObjectInfo()` what is under it. Picking an object
  is a screen-space question that the world point cannot answer.
- Translations updated for everything new in this release: 18 strings across
  all eight languages (de, fr, hi, hu, ko, ru, sr, zh-CN), recompiled to `.qm`.
  `update_translations.py` now does the extraction in a single `lupdate` pass
  over the Python and C++ sources and merges into every language file, instead
  of the old two-pass route through `lconvert` -- which PySide6 does not ship,
  so the script could not run with pip-installed Qt tools at all.

- **The demo line can now contain arcs.** Press **M** (`ToggleSegmentMode`) and
  the next segment becomes an arc: click its endpoint, then a point anywhere
  along it. Three points, so no tangency is imposed -- segments may meet at any
  angle -- and they define their own plane, so nothing is restricted to 2D. A
  degenerate arc quietly becomes a straight segment. Polylines that stay
  straight are still made as editable Draft wires; once an arc is involved the
  result is a `Part::Feature` carrying one wire of mixed line and arc edges,
  since a Draft wire cannot hold arcs.
  While the point on the arc is being picked the compass sits on the midpoint of
  the chord -- which is what the bulge is measured from -- and moves to the
  arc's end once the segment closes.
  AxisFlow itself knows nothing about arcs: the key is relayed through the new
  `modecallback` of `getPoint()` (mirroring `finishcallback`), and what the mode
  means is entirely the consumer's business. New `AxisFlow.setBasePoint()` moves
  the compass anchor without confirming a point.

- **The demo line can be snapped to itself while it is being drawn.** The
  confirmed part of the current line now lives in a throw-away
  `Part::Feature`, rewritten after every point, so endpoint, midpoint,
  perpendicular and intersection snaps all work against the line you are in
  the middle of drawing. This is how Draft's own line tool does it, and it has
  to be: the Snapper only ever considers document objects (it picks them with
  `View.getObjectsInfo()` and resolves them through `ActiveDocument`), so a
  Coin node in the scenegraph -- which is what the preview used to be -- can
  never be a snap target. The scratch object is removed before every commit,
  so it reaches neither the saved document nor the undo stack, and the Coin
  preview is now just the rubber band. Nothing changes in AxisFlow itself:
  this is entirely the consumer's side, in `axisflow_demo.py`.

- **The compass path.** A thin dashed poly-line spelling out how the
  *prospective click point* (PCP -- the point that would be placed right now,
  carrying every lock) is reached from the compass centre, continuing to the
  snap point when the two differ. It appears while an object snap is live, or
  while any coordinate field is frozen (the panel check box).
  The shape follows the frozen in-plane fields, so the path reads back what
  those fields say:

  | frozen | path |
  |---|---|
  | nothing | centre -> PCP projected on the plane -> PCP |
  | X | centre -> along X -> plane projection -> PCP |
  | Y | centre -> along Y -> plane projection -> PCP |
  | X and Y | centre -> along X -> along Y -> PCP |

  A frozen Z only fixes the length of the last leg. Legs of zero length are
  dropped. Settings live in the **Live Line** section, where the other line
  drawing lives: "Draw path to the point" (on by default -- switch it off and
  no path is drawn), "Path style" and "Path colour".

### Fixed
- **Del deletes the selected object again while the compass is on.** Backspace
  and Delete were claimed unconditionally -- by the router outside the fields,
  and by the line edit inside them -- so they never reached FreeCAD. They now
  belong to AxisFlow only while a value is actually being TYPED: focus is not
  the test, since the caret is parked in a coordinate field after every click.
  Type something and Backspace trims it, Del clears it; type nothing and both
  go to FreeCAD. (Qt needed one extra step: a focused QLineEdit accepts the
  ShortcutOverride for editing keys and thereby suppresses the shortcut, so the
  panel now blocks that override -- without accepting it -- for those two keys
  when nothing is being typed.) Arrow keys, Home and End still belong to the
  field.
- **The Activate button works on the first press while a line is being drawn.**
  The palette command and the Python API each owned a tool of their own and
  each looked only at its own pointer, so pressing Activate during a getPoint
  session built a *second* tool on the same view: the press appeared to do
  nothing, and the next one switched that second tool off. There is now one
  live session at a time (`AxisFlowTool::current()`), and the button means "end
  whatever AxisFlow is doing" -- a running consumer is told through the same
  channel its cancel key uses, so it commits or aborts its geometry and drops
  its scratch objects instead of being cut off.
- **Creating a new file while drawing, then drawing in it, no longer crashes.**
  Handling a mouse move calls into Python -- for snapping, and to deliver the
  live point to the getPoint consumer -- and the consumer may end the session
  from there, which the line demo does as soon as it notices the document
  changed. Ending it tore down the Coin event callback synchronously: the very
  node Coin was dispatching through, and whose setHandled() was called moments
  later. The teardown is now deferred to the event loop whenever it is
  requested from inside that callback. (The keyboard path never hit this, so
  ending with the backtick always looked fine: keys arrive through Qt, not
  through Coin.)
  The line demo also tears its previous run down properly instead of dropping
  it for the garbage collector -- which used to free the Python wrappers of
  Coin nodes that were still children of a live scenegraph -- remembers which
  scenegraph its preview went into rather than asking for the active view's,
  and removes its scratch object from the document it started in.
- **Closing a file while AxisFlow is running no longer crashes FreeCAD.**
  Closing a document destroys its 3-D view, and with it the viewer, scenegraph
  and camera the tool hangs off. Three separate paths led into freed memory:
  the Activate command kept a raw pointer to a tool Qt had already deleted as a
  child of the view; the tool's destructor tore down through a view that was
  itself mid-destruction; and the Python session was created with no parent at
  all, so it outlived its view and went on using it. Nothing anywhere was
  listening for a document closing. The view is now held weakly, the tool tears
  itself down when its view dies, both owners clear their pointers, and the
  camera the size sensor watches is referenced for as long as it is watched.
  The demo command follows the document you are actually in instead of
  capturing one for the whole command.
- **Freezing a coordinate no longer switches axis indexing off.** Lock X with
  its hotkey, slide along it, and the axis snap had gone. Indexing runs after
  the locks and rewrites the coordinates, so it used to be disabled wholesale
  whenever anything was frozen -- otherwise it would have overwritten the
  frozen value. The permission is now per axis: riding the X axis collapses Y
  and only needs Y free, riding the Y axis collapses X and only needs X free.
  Typed values are protected the same way, and in radial mode, where a
  projection onto an axis changes both distance and angle, a fixed
  distance/angle still rules it out entirely.
- **Draft's own track line no longer appears when an object snap fires.**
  AxisFlow already asked for it to be suppressed (`noTracker=True`), but Draft
  honours that flag only on the path where nothing was snapped: once a snap
  wins, `Snapper.snapToObject()` switches the track line on unconditionally --
  it is not even passed the flag. That produced a thin solid line in Draft's
  default colour, drawn from the compass centre to the snapped point on top of
  AxisFlow's own guide line and compass path. It is now switched off
  explicitly; the snap marker itself is untouched.
- The compass no longer leaks its whole subgraph once per session: the
  constructor referenced the root node and nothing ever released it.

### Added
- **Two-key chords (MicroStation style).** A binding can now be a pair of keys
  pressed in sequence: press the prefix, then the key that completes it. Three
  ship by default -- `R X`, `R Y`, `R Z` -- for the new **RotateAboutX / Y / Z**
  actions, which turn the compass 90 degrees about its *own* X, Y or Z axis.
  Unlike RotateTop/Front/Side these are relative: they work from wherever the
  compass currently is.
  - While a prefix is pending, a small hint window appears beside the
    coordinate panel listing what is on offer, one option per line. It is a window of its own, so the panel never resizes, and it
    never takes focus, so the caret stays in the active coordinate field.
  - Any key that does not complete a chord cancels the wait and is swallowed,
    so a mistyped `R`,`Y` never silently freezes the Y field. Escape cancels
    the wait too. There is no timeout.
  - **The single/chord rule:** a key is either a binding of its own or the
    first key of a chord, never both -- otherwise a prefix press could only be
    resolved by waiting to see whether a second key follows. Bindings that
    would break it are refused by `setHotkeys()`, by the `hotkeys.ini` parser
    (with a Report view warning) and by the settings dialog (with an
    explanation). Chords are capped at two keys for the same reason.
  - In the settings dialog, hold **Shift** while pressing the first key to
    record a chord; the button then waits for the second.
  - Formats: `hotkeys.ini` joins a chord with a space (`RotateAboutX = R X`)
    while a comma still separates alternative keys (`SmartLock = Return,
    Enter`). The Python API takes and reports a chord as a nested pair:
    `AxisFlow.setHotkeys("RotateAboutX", [[Qt.Key_R, Qt.Key_X]])`.

### Changed
- The settings dialog has a **"Restore defaults" button on every section**,
  writing back only that section's options -- adjusting the compass no longer
  means risking your indexing tolerances or hotkeys. Both hotkey pages carry
  the key-reset button now. New `AxisFlow.getDefaultSettings()` exposes the
  factory defaults (same keys as `getSettings()`); they moved out of the
  settings .cpp into a public `AxisFlowDefaults` struct, still a single source
  of truth.
- The dialog's section list sizes itself to its longest label instead of a
  fixed 104 px, so translated names (Russian in particular) are no longer cut.
- **The "Toggle live line" button is off the workbench palette.** The live line
  is a checkbox in the settings dialog, which is where a single boolean
  belongs. The command stays registered, so it can be added to a custom
  toolbar or called from a macro.
- **Key handling is now one policy in one place.** The decision "is this key
  ours, and what does it mean?" used to be re-implemented in the panel's field
  filter, in the panel's `keyPressEvent`, in the application-wide event filter,
  and -- partially -- in the Coin event callback. It now lives in the pure,
  header-only `routeKey()` (`src/AxisFlowKeyRouting.h`, no Qt/Coin, covered by
  31 Tier-1 tests); `src/AxisFlowKeyContext.h` is the single Qt adapter that
  turns a `QKeyEvent` into its input. The call sites only execute the verdict.
  Nothing changes for users: `hotkeys.ini`, the settings dialog and
  `setHotkey()` behave exactly as before. Adding an action is now one line in
  `hotkeys.ini` plus one in `Hotkeys::setDefaults()`.

### Fixed
- **Modified keys no longer trigger AxisFlow actions.** The panel's field
  filter compared bare key codes, so `Ctrl+Z` inside a coordinate field fired
  the Z lock instead of Undo (and any `Ctrl`/`Alt` combination whose bare key
  was bound behaved the same). Modified keys are now always left to FreeCAD.
- The Coin event callback no longer reads the keyboard. It carried a second key
  map that never learned the actions added after it was written (`P`, `O`, `T`,
  `F`, `S`, `V`, `E`, backtick) and ignored modifiers entirely. Keys are claimed
  at the Qt level, as documented -- the callback only handles the cursor and
  clicks now.

### Removed
- `AxisFlowService::solver()` -- the escape hatch to the raw solver, unused by
  any caller in or outside the module. State is reached through the service, as
  intended, which keeps the future singleton -> session migration cheap.
- Dead `AxisFlowTool::handleEscape()` (Escape has defaulted to `FinishLine`
  since the layered cancel moved to the panel).

### Changed
- **Lock semantics reworked (breaking).** A lock now FREEZES a coordinate field
  at its current value (the checkbox next to the field is its visual): the
  frozen component stops following the mouse, the free ones keep moving.
  `LockX`/`LockY` act in orthogonal mode only, `LockZ` works in both modes and
  with an object snap pins the snapped point's height in compass space. The old
  single-axis lock (which zeroed the *other* components) is gone from the
  solver; "movement along one axis" is now the composition of two field locks.
- **SmartLock** now freezes the *other two* fields at 0 by the live-line
  direction: cursor along X locks Y=0 and Z=0 (movement only along X), along Y
  locks X=0 and Z=0. In radial mode it locks the angle to the nearest
  0/90/180/270 deg plus Z=0, leaving the distance free. Pressing it again (or
  any lock state) releases all locks. All locks stay per-point: placing a point
  clears them. While a lock pins the point onto an axis (the SmartLock pair, a
  field frozen at exactly 0, or a polar angle at a multiple of 90 deg) the
  **candidate axis stays visible** as feedback that the cursor is riding it.
- Python API: `lockAxis()` accepts `'x'|'y'|'z'|'d'|'a'|'none'` and is an
  idempotent "freeze at current value"; `getState()['lock']` is replaced by
  `getState()['locks']` (a per-slot bool dict). New `setDisplayLock()` is the
  adapter-only display channel that mirrors Draft's restriction in the panel
  checkboxes (Draft's "move along X" shows as frozen Y and Z).

### Changed (settings)
- The **"Snapping" settings section is renamed to "Behaviour"** -- it now hosts
  general input-behaviour options, not just snapping.
- **AccuDraw sign-from-direction is now optional**: the new "Smart negative
  direction" checkbox (Behaviour section, `smartNegativeDirection` via the
  Python API, default ON) keeps the current behaviour -- a bare magnitude typed
  into a local field takes the sign of the cursor's direction along that axis,
  and the field reformats to the signed value on blur. Switched off, the typed
  sign is used literally regardless of where the cursor points.

### Removed
- The **"Smart lock" checkbox** in the Snapping settings section. The behaviour
  it gated is now unconditional: while the cursor rides a compass axis (within
  the axis-indexing tolerance), an object snap contributes its along-axis
  coordinate instead of pulling the point off the axis -- the point always
  stays on the axis the cursor moves along. Requires axis indexing to be
  enabled; with an explicit field lock the lock itself already pins the point.
  The stored `SmartLock` parameter key is no longer read (leftovers on disk are
  harmless). Python: the `smartLock` entry is gone from `getState()`,
  `getSettings()` and `setSetting()`.

### Fixed
- **ToggleActive is no longer a one-way switch.** The key filter used to stand
  down entirely while AxisFlow was off, so the very key meant to turn it back
  on never fired. While off, the filter now claims exactly one key -- the one
  bound to ToggleActive -- and performs it.

### Added
- **Compass rotation animation** ("Behaviour" section,
  `compassRotateAnimation` via the Python API, seconds, default 0.5, 0 =
  instant): the compass eases into a new orientation (slerp with a smoothstep
  ease-in/out) instead of snapping, so RotateTop/Front/Side/ByView/Cycle and
  "compass follows direction" are visibly readable. Purely a display effect --
  the solver switches planes immediately and input never waits; the live guide
  line stays glued to the cursor throughout the sweep.
- **View / origin hotkeys**: `SetOrigin` (`O`) moves the compass origin to the
  current solved point (object snap included) *without* confirming a point --
  the running draw command ignores it. `RotateTop` (`T`) aligns the compass
  with the global axes; `RotateFront` (`F`): compass Z along global -Y, compass
  Y along global +Z; `RotateSide` (`S`): compass Z along global +X, compass Y
  along global +Z; `RotateByView` (`V`) matches the current camera;
  `RotateCycle` (`E`) cycles Top -> Front -> Side. Defaults follow the shipped
  `hotkeys.ini`; the built-in fallback layout now mirrors it (`ConfirmPoint` =
  `P`, `SmartLock` = `Enter`, `Insert` freed). The Draft adapter unbinds the
  new letters for the duration of its session.
- The settings dialog hotkey page is split into **Control hotkeys** and **View
  hotkeys**; the `ConfirmPoint`, `ClearLocks` and `CancelEntry` rows are no
  longer shown (still bindable via the API or `hotkeys.ini`), which makes the
  window shorter.

- **Compass follows direction** ("Behaviour" section, `compassFollowDirection`
  via the Python API, default Off): after each confirmed point the compass
  re-aims its +X axis along the segment just drawn -- AccuDraw's
  rotate-to-segment. "On (keep Z)" rotates in the current plane only (the
  segment is projected, the normal is untouched); "On (full)" takes the 3-D
  direction, keeping the normal as close to the previous one as possible. The
  first point of a fresh session never rotates (no previous segment yet); a
  base point seeded via getPoint's 'last' counts as one.
- **Focus advance on movement** ("Behaviour" section, `focusAdvanceOnMove` via
  the Python API, default ON): after typing a value into an in-plane field, a
  deliberate cursor move (beyond ~6 px) finishes the entry and puts the caret
  into the adjacent field -- type X, move, type Y, without reaching for Tab.
  Works for D <-> A in radial mode; mouse jitter while typing never steals the
  entry, and the anchor re-arms on every keystroke.
- **`LockD` action** (default key `D`): freeze the distance field in radial
  mode. Bindable via `hotkeys.ini` / `setHotkey(..., 'lockd')` / the settings
  dialog.
- The radial (polar) panel now shows all **three fields — D / A / Z** (the Z row
  is no longer hidden), with Tab cycling across all of them.
- Polar locks in the solver: freezing the angle constrains the point to a ray,
  freezing the distance fixes the length (closes the former "polar branch
  ignores the lock" limitation; the `DISABLED_` placeholder test is now two
  live tests).

## [0.1.3] - never released

> Tagged in this file but never published; everything below shipped as part of
> 0.2.0.

### Added
- Windows GUI installer (`setup.exe`, NSIS): per-user install into the FreeCAD
  Mod folder with an uninstaller registered in *Apps & features*. Built by
  `build_installer.bat` from `installer/AxisFlow.nsi`.
- Linux one-command install script (`install.sh`): `curl … | bash` downloads a
  prebuilt tarball from Releases, verifies its checksum and refuses to install on
  a non-1.1.x FreeCAD. **Not usable yet** — no Linux binary is published (it needs
  the cross-platform build, still in progress); the consumer script is ready.
- Shared release staging (`stage_dist.bat` / `stage_dist.sh`) as the single
  source of truth for the shipped runtime files.
- **Object snapping**: AxisFlow now uses FreeCAD's own snaps (endpoint, midpoint,
  centre, intersection, perpendicular, near, extension, parallel, angle, ortho,
  grid, ...) inside its own tools, with Draft's snap palette added to the
  AxisFlow toolbar so the state is shared. A snapped point takes precedence over
  soft indexing and is reported in the X/Y/Z fields with its true 3-D
  coordinates. New **SmartLock** option: on a snap the candidate axis freezes at
  the perpendicular projection of the snapped point onto it.
- **Hotkeys** section in the settings dialog: rebind any action by pressing a
  key, unassign it, or restore the defaults, with a warning when a key is
  already taken. New API: `AxisFlow.setHotkeys(action, keys)`,
  `AxisFlow.resetHotkeys()`.
- Editable default hotkey layout in `hotkeys.ini` (Mod folder): change the
  default keys without rebuilding. Layered built-in -> file -> user bindings, so
  it never overrides a key the user rebound; typos are reported in the Report
  view. New API: `AxisFlow.loadHotkeyDefaults(path)`.
- Interface translation (i18n) following FreeCAD's UI language (no separate
  language switch; matches FreeCAD on first start). Strings marked in the Python
  dialog and the C++ commands/panel. Bundled translations: German, French,
  Hungarian, Chinese (Simplified), Korean, Hindi, Serbian and Russian (initial,
  machine-assisted). Tooling: `update_translations.py`; guide: `TRANSLATING.md`.

### Changed
- Default settings tuned for a better out-of-the-box look (compass size 100, axis
  start/length 0.80/0.35, live line off / solid / cyan, distance & axis indexing
  on, tolerance 15). Existing configs are unaffected; run `AxisFlow.resetSettings()`
  to adopt the new defaults.
- Settings dialog reorganised into left-hand sections (**Compass** / **Live Line**)
  instead of one long list.

## [0.1.2] - 2026-07-17

### Added
- **Live-line indexing** (local coordinate frame only), snapping the live line
  as the cursor approaches a remembered value within a pixel tolerance:
  - *Distance indexing* — a **candidate point** appears when the live-line
    length nears a remembered "indexed distance"; clicking places the point at
    that exact length while keeping the current direction.
  - *Axis indexing* — a styled **candidate axis** replaces the live line when it
    nears the X or Y axis; clicking places the point on that axis
    (perpendicular projection). Distance and axis indexing compose: the point
    lands on the axis at the indexed distance.
- Six settings: `Use distance indexing`, `Use axes indexing`,
  `Indexing tolerance` (0–50 px), `Live line transparency`,
  `Indexing axis weight`, `Indexing axis transparency`.
- Python API: `setIndexedDistance()` / `clearIndexedDistance()`; indexing fields
  in `getState()` (`indexedDistance`, `indexCandidatePoint`, `indexCandidateAxis`)
  and the new settings keys in `getSettings()` / `setSetting()`.
- Round compass dots: the centre point and the candidate point are now rendered
  as filled discs (point antialiasing) instead of squares.
- Tier-1 unit tests for the indexing policy and solver integration
  (`tests/test_indexing.cpp`) and matching Tier-2 API tests.

### Changed
- The coordinate fields now mirror the **snapped** candidate while a candidate
  point or candidate axis is active — the off-axis field reads `0.000` on an
  axis snap, the length follows the indexed distance on a point snap — and fall
  back to the raw cursor when no candidate is active.
- The candidate point is suppressed while a coordinate is being typed into a
  field: the entered value takes precedence over the snap.
- The build now links `OpenGL::GL` (required for the antialiased points).

### Fixed
- The coordinate read-out no longer flickers between `0.000` and `-0.000` as a
  value crosses zero: values that round to zero are pinned to a clean `0.000`.
- Version numbers are synchronised across `package.xml`, `CMakeLists.txt`,
  `make_release.bat` and `../README.md` (the CMake project version had lagged at
  `0.1.0`).

## [0.1.1] - 2026-07-12

Baseline alpha — the AccuDraw-style precision input assistant as first packaged
with FreeCAD [Package Metadata](https://wiki.freecad.org/Package_Metadata)
(`package.xml`). Highlights:

- On-canvas **compass** (Coin) oriented to the working plane, held at a constant
  screen size through zoom, with configurable axis colours, thickness, fill and
  a live guide line.
- **Coordinate panel** with unit-aware entry: X/Y/Z in rectangular mode, D/A in
  polar mode, an active-field focus model, and a Local/World frame toggle.
- **Axis locks** (X/Y/Z), keyboard value overrides, and nearest-axis smart lock.
- Configurable, persisted **hotkeys**.
- Python `getPoint`-style bridge for consumers, with continuous and single-point
  sessions; a cooperative **Draft/Arch/BIM snap adapter**.
- Settings dialog with persistence via FreeCAD's `ParameterGrp`, and a public
  **Python API** (`AxisFlow` module).
- Tier-1 (C++ gtest) and Tier-2 (Python) test suites.

> History before 0.1.2 is summarised: this changelog was introduced in 0.1.2, so
> the entry above describes the project's state at that point rather than a
> granular per-release diff.

[0.2.1]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.2...v0.2.0
[0.1.3]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/LeonidWiebe/AxisFlow/releases/tag/v0.1.1
