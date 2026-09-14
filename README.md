# pipegen

**Two-stroke expansion chamber designer** — cross-platform C and SDL2.

pipegen designs an expansion chamber for a two-stroke engine from its port
timing, bore, stroke and the speed it has to work at; shows it in 3D beside
the engine, where it can be bent at its joints to fit the space available;
and writes flat patterns for laser cutting — a DXF for the laser and a PDF
for the workshop — for chambers that are rolled and welded or hydroformed.

The default project is a **JLO L372 driving a generator at 2850 rpm**.

> **Status.** Built and tested against its own unit tests (ASan + UBSan) and
> by driving the running program under a virtual display. No chamber has yet
> been cut, rolled or run from its output. See *What has and has not been
> proven* below before cutting steel.

## Features

- **Projects** — plain-text `.pgp` files, undo/redo, recent projects, unsaved-
  changes protection.
- **New Project wizard** — engine, objective, manufacture, installation,
  review.
- **Objectives** — power at a fixed rpm, power across a band, economy, noise
  reduction. Each sets the chamber's proportions, all of which can then be
  edited.
- **3D design view** — orbit, pan, zoom, pick pieces and joints; colour by
  section or by part; engine stub for scale; optional clearance box.
- **Bending** — select a joint and Ctrl+drag (or type the angle and direction).
  Joints are mitred on the bisecting plane, so the centreline — and the wave
  timing — does not change. Clashes with the clearance box, the engine or the
  chamber itself turn the pieces red; joints bent more sharply than the bend
  rules allow get an amber ring.
- **Auto-fold** — folds a long chamber into the most compact layout, or into
  the clearance box, that keeps every bend rule: no joint sharper than its
  section allows (30° header, 25° diffuser, 20° belly and baffle, 15° into the
  stinger), no run of bends tighter than 2 diameters, a heat gap from the
  engine and from itself. Every turn is spread over several joints. The
  winner is re-checked with the full surface check before it is applied.
- **Rolled and welded** — each piece unrolled on the mean diameter: exact
  annular sectors and rectangles for square ends, point-by-point development
  for mitred ends, seam allowance, alignment marks, header/stinger from tube.
- **Hydroformed** — two flat halves, half the mean circumference wide plus a
  weld margin, with first-order seam-length compensation, in-plane bends, and
  automatic splitting of halves longer than the sheet.
- **Output**
  - DXF (AutoCAD R12, mm): all sheets in one file, and/or one file per part.
    Layers `CUT`, `ETCH`, `LABEL`, `SHEET`. Arcs stay true arcs.
  - PDF: design sheet, wave-timing plot and cut list, sheet layouts, and
    every part at full size with a 100 mm check bar.
- **Headless** — `--export` and `--render` for scripts and CI.
- Built-in manual (F1), tooltips on every control, dark and light themes.

## Build

Dependencies: SDL2 only. Nuklear is vendored in `third_party/`.

```sh
sudo pacman -S sdl2            # Arch
sudo apt install libsdl2-dev   # Debian / Ubuntu
brew install sdl2              # macOS

make            # ./pipegen
make test       # unit tests under ASan + UBSan
make run
```

Windows (cross-compiled with mingw-w64) and a portable Linux AppImage use the
same tooling as svpview and magview:

```sh
tools/win/get-sdl2.sh && make windows-dist   # dist/pipegen-<ver>-win64.zip
make appimage                                # dist/pipegen-<ver>-x86_64.AppImage
make release                                 # both, plus SHA256SUMS.txt
```

## Use

```sh
./pipegen                                   # opens the wizard the first time
./pipegen --project my.pgp
./pipegen --project my.pgp --export out/ my-chamber    # no window
./pipegen --project my.pgp --render view.ppm 1600 900  # no window
./pipegen --project my.pgp --autofold compact --save folded.pgp
./pipegen --help
```

| In the 3D view | |
|---|---|
| Left drag | orbit |
| Right or middle drag, Shift + left drag | pan |
| Wheel | zoom about the pointer |
| Click | select a piece or a joint ring |
| Ctrl + drag on a selected joint | bend it (vertical: angle, horizontal: direction; + Shift to step) |
| `[` `]` / `,` `.` | angle / direction of the selected joint |
| Page Up / Page Down | previous / next joint |
| Delete | straighten the selected joint |
| `F`, double-click | fit |
| `1`–`4` | 3/4, side, top, end views |
| Ctrl+Z / Ctrl+Y | undo / redo |

Route > **Auto-fold** chooses the bends for you: most compact or fit the box,
any direction or flat or upright, with limits on turns, segments and gaps.

The full manual is in the program (F1) and in [docs/HELP.md](docs/HELP.md),
generated from the same source by `make help-doc`.

## How the chamber is designed

Wave timing, in the tradition of Jennings and Blair. The pressure pulse that
leaves the port when it opens reflects from the baffle and must get back as
the port closes at the design speed, so the tuned length from the port face
to the middle of the baffle is

    L = a · ED / (12 · N)        [m, with a in m/s, ED in degrees, N in rpm]

with the wave speed `a = sqrt(1.35 · 287 · T)`. Section lengths are fractions
of `L`, diameters multiples of the outlet bore, both set by the objective.
The Profile page and the PDF plot when the suction and plugging waves reach
the port across the speed range.

It gives a sound starting geometry. **It is not a gas-dynamic simulation.**

## What has and has not been proven

Proven by the tests (`make test`):

- every rim of every rolled cone unrolls to exactly its circumference;
- a developed mitre edge is as long as the 3D curve it wraps to, and every
  generator keeps its length;
- hydroformed stations are half the mean circumference plus margin, and with
  compensation each flat edge matches its inflated wall;
- bending preserves the centreline length; mitre planes of adjacent cylinders
  meet exactly; clash detection finds box, engine and self-intersections;
- the bend rules flag a sharp joint and a tight run; auto-fold's results keep
  every rule when rebuilt and checked in full, fold the JLO chamber to under
  half its straight length, fit a box it cannot fit straight, refuse a box it
  cannot fit at all, keep hydroformed bends in the seam plane, and repeat
  exactly for the same seed;
- the DXF is structurally sound (sections, polylines, bulges) and every PDF
  xref offset points at its object;
- picking in the 3D view returns the piece and joint under the pointer.

**Not yet proven:**

- **No part has been cut and rolled.** Check the first set against the
  full-size PDF templates before committing a sheet.
- **Hydroforming compensation is a first-order model.** Inflate a test
  piece.
- **The bend rules are rules of thumb**, not a flow simulation. A fold that
  keeps them should behave very close to the straight design; confirm it on
  the engine.
- **The JLO L372 preset's port timing and outlet are not factory figures.**
  Bore and stroke (80 × 74 mm) are published; the 155° exhaust duration is a
  figure quoted by owners, and the 38 mm outlet and 40 mm duct are
  placeholders. Measure them.
- The DXF has been checked structurally, not yet opened in a laser's CAM.
- The Windows build is cross-compiled; it has not been run on Windows.

## Layout

| | |
|---|---|
| `src/pg_project.*` | project model and file format |
| `src/pg_design.*` | wave-timing design: sections, timing band, checks |
| `src/pg_route.*` | pieces, joints, bends, mitres, bend rules, engine stub, clashes |
| `src/pg_autofold.*` | the auto-fold search |
| `src/pg_pattern.*` | flat patterns and sheet nesting |
| `src/pg_geom.*` | bulge polylines: exact arcs, boxes, areas |
| `src/pg_dxf.*`, `src/pg_pdf.*`, `src/pg_report.*` | output |
| `src/pg_raster.*`, `src/pg_view3d.*` | software rasteriser and 3D scene |
| `src/pg_model.*` | a project and everything derived from it; export |
| `src/pg_ui*.c`, `src/pg_draw.*`, `src/pg_theme.*` | interface |
| `src/plat_posix.c`, `src/plat_win32.c` | the only OS-specific code |

No file above `plat.h` calls an OS API, and nothing below the UI touches SDL
or Nuklear, which is why the whole design core runs in the tests and in the
headless options.

## Licence

GPL-3.0-or-later. Copyright (C) 2026 Hugh Frater. See [LICENSE](LICENSE).
Nuklear (`third_party/`) is MIT or public domain; SDL2 is zlib-licensed;
DejaVu Sans (bundled in the AppImage) has its own permissive licence.
