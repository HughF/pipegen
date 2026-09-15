# Changelog

All notable changes to pipegen. Versions are dates (YYYY.MM.DD), with .N for
a second release on the same day.

## [Unreleased]

## [2026.09.15.1] — Windows fixes

### Fixed

- The unsaved-changes question had no buttons whenever its sentence fitted on
  one line (a short project title, or a wide enough dialog, as seen on
  Windows). Measuring its height fed back on itself and shrank the dialog by
  16 px a frame until only the title bar was left.
- A file that cannot be written now says why ("Permission denied", and so on)
  instead of only "Cannot write". On Windows a refusal also points at
  Controlled folder access, which blocks unrecognised programs from Documents;
  pipegen.exe has to be allowed there, or the project saved elsewhere.
- Errors in the file dialog wrap to their full length instead of being cut at
  two lines, and an old save error no longer shows up in About or the
  unsaved-changes question. A message too long for the status bar shows in
  full when the pointer is over it.

## [2026.09.15] — first release

### Changed

- The 3D view and the interface are drawn with OpenGL 3.3. Orbiting a
  1900 × 1200 view went from 41–64 ms a frame in software to under 3 ms
  (AMD Radeon, Mesa), with 4× multisampling and per-pixel lighting; picking
  reads the surface id from a second GPU pass. Without OpenGL 3.3 the program
  falls back to the software renderer by itself; `PIPEGEN_RENDERER=software`
  forces it, and About shows which is in use. The PDF, `--render` and the
  tests still use the software rasteriser, from the same scene mesh.

### Added

- Twist the rest of the pipe at a joint: everything past it turns as one
  piece round the pipe coming in, with later bends keeping their shape — for
  wrapping the chamber round an obstacle. Twist buttons on the joint panel
  (half turns only when hydroformed), Shift+comma and Shift+full stop.
- Joint rings are picked within about 10 pixels, not only on their few
  pixels of line, unless something else is under the pointer in front.
- Documentation of hydroforming compensation (README and manual): what seam
  compensation corrects — width, cone length, and bends closing up as the
  pipe inflates, with the flat-turn formula and a table — and what it does
  not: stretch, springback, weld shrinkage, incomplete inflation.
- Screenshots of the program and its PDF in the README (docs/screenshots/).
- Auto-fold (Route tab, and `--autofold compact|box` on the command line):
  searches for the most compact layout, or the roomiest fit in the clearance
  box, that keeps every bend rule and gap, spreading each turn over several
  joints and adding segments where a gentler turn needs them; re-checks the
  winner in full before applying it; undoable, with Revert. `--save FILE`
  writes the folded project.
- Bend rules for every joint: at most 30° a joint in the header, 25° in the
  diffuser, 20° in the belly and baffle, 15° into the stinger, and no run of
  bends tighter than 2 diameters. Breaking joints get a warning, an amber
  ring in the 3D view and a note in the joint panel.

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
