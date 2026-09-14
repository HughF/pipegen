# Changelog

All notable changes to pipegen. Versions are dates (YYYY.MM.DD).

## [Unreleased]

### Added

- Expansion chamber design by wave timing: tuned length from exhaust
  duration, design speed and mean gas temperature; section proportions and
  diameter ratios per objective (power at a fixed rpm, power across a band,
  economy, noise reduction); timing band; design checks.
- Default project: JLO L372 generator at 2850 rpm. Bore and stroke are the
  published 80 × 74 mm; exhaust duration, outlet bore and duct length are
  flagged as values to measure.
- Projects as plain-text `.pgp` files; undo/redo; recent projects;
  unsaved-changes prompt.
- New Project wizard: engine, objective, manufacture, installation, review.
- 3D design view with a software depth-buffer rasteriser: orbit, pan, zoom,
  picking, colour by section or part, engine stub, clearance box, grid.
- Bending at segment joints with mitred pieces; Ctrl+drag, keyboard and
  inspector controls; clash detection against the clearance box, the engine
  and the chamber itself; hydroformed bends constrained to the seam plane.
- Flat patterns: rolled cones and cylinders on the mean diameter, developed
  mitre edges, seam allowance, alignment marks; hydroformed halves with
  seam-length compensation and splitting to the sheet; header and stinger
  from tube; shelf nesting on sheets.
- DXF (R12, mm) layout and per-part output with CUT, ETCH, LABEL and SHEET
  layers; PDF design sheet, timing plot, cut list, sheet layouts and
  full-size templates.
- Headless `--export`, `--render`, `--save-default`, `--help-doc`.
- Built-in manual and tooltips; dark and light themes; Windows cross-build
  and AppImage packaging from the svpview/magview tooling.
