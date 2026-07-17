# Changelog

All notable changes to **AxisFlow** are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
While the project is in `0.x` (alpha), the public API may still change between
minor releases.

## [Unreleased]

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
  `make_release.bat` and `README.md` (the CMake project version had lagged at
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

[Unreleased]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.2...HEAD
[0.1.2]: https://github.com/LeonidWiebe/AxisFlow/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/LeonidWiebe/AxisFlow/releases/tag/v0.1.1
