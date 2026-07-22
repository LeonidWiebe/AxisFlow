# Changelog

All notable changes to **AxisFlow** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is in `0.x` (alpha), the public API may still change between
minor releases.

## [Unreleased]

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

## [0.1.3] - unreleased

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

[Unreleased]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.3...HEAD
[0.1.3]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.2...v0.1.3
[0.1.2]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/LeonidWiebe/AxisFlow/releases/tag/v0.1.1
