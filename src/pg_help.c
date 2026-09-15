/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Hugh Frater
 *
 * This file is part of pipegen. pipegen is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version. It is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License in LICENSE for details.
 */
/*
 * pg_help.c — the manual
 *
 * Written for someone who will cut steel from the output, so it says what to
 * do and where the numbers come from. The tooltips give the one-line version
 * of the same thing; this is where the reasons live.
 */
#include "pg_help.h"

#include <stdbool.h>

#define T(s)      { PG_HELP_TEXT,   (s),  NULL }
#define SUB(s)    { PG_HELP_SUB,    (s),  NULL }
#define B(s)      { PG_HELP_BULLET, (s),  NULL }
#define R(k, v)   { PG_HELP_ROW,    (k),  (v)  }
#define N(s)      { PG_HELP_NOTE,   (s),  NULL }

static const PgHelpItem it_about[] = {
    T("pipegen designs an expansion chamber for a two-stroke engine from its "
      "port timing, bore, stroke and the speed it has to work at. It shows "
      "the chamber in 3D beside the engine, lets it be bent at its joints to "
      "fit the space available, and turns it into flat patterns: a DXF for "
      "the laser and a PDF for the workshop, with a design sheet, a cut list, "
      "the sheet layouts and full-size templates."),
    T("Designs are saved as projects (.pgp files). A new project is set up "
      "with a wizard; everything it asks can be changed afterwards in the "
      "Design page's inspector, and every view updates as you type."),
    N("The design comes from wave-timing rules, not from a gas-dynamic "
      "simulation. It is a sound starting point. Measure the exhaust timing "
      "and outlet bore on the engine itself, and expect to fine-tune the "
      "header and stinger on test."),
};

static const PgHelpItem it_start[] = {
    T("The first time pipegen runs it opens the New Project wizard, filled in "
      "for a JLO L372 driving a generator at 2850 rpm. Step through it, "
      "changing what differs for your engine, and press Create."),
    R("1  Engine",       "Bore, stroke, cylinders, exhaust duration, outlet "
                         "bore and the length of the exhaust duct in the "
                         "casting. One chamber is designed per cylinder."),
    R("2  Objective",    "What the chamber is for: power at a fixed speed, "
                         "power across a band, economy or noise reduction, "
                         "and the speed or band to design for."),
    R("3  Manufacture",  "Rolled and welded, or hydroformed; material, "
                         "thickness, segments and sheet size."),
    R("4  Installation", "An optional clearance box the chamber must stay "
                         "inside, or out of."),
    R("5  Review",       "The resulting chamber and anything worth checking."),
    T("Save the project from the rail. The status strip and the title bar "
      "mark unsaved changes with an asterisk, and pipegen asks before "
      "discarding them. Undo and Redo (Ctrl+Z, Ctrl+Y) step back through "
      "every change, bends included."),
};

static const PgHelpItem it_engine[] = {
    R("Bore, stroke",     "Give the swept volume, which appears on the design "
                          "sheet and sizes the silencer for the noise "
                          "objective. They also size the engine drawn in 3D."),
    R("Exhaust duration", "Crank degrees the exhaust port is open. Measure it "
                          "with a degree wheel: the angle after TDC at which "
                          "the port starts to open, then 2 x (180 - that). "
                          "The tuned length is directly proportional to it."),
    R("Outlet bore",      "Internal diameter where the chamber starts: the "
                          "exhaust port outlet or the manifold flange. Every "
                          "diameter in the chamber is a multiple of it."),
    R("Duct in casting",  "The exhaust passage from the port face to the "
                          "flange. It is part of the header acoustically, "
                          "but it is not made, so it is left off the patterns."),
    R("Port angled down", "How far below horizontal the exhaust leaves the "
                          "cylinder, which sets the direction the chamber "
                          "starts in."),
    N("The JLO L372 preset has the published 80 x 74 mm bore and stroke. Its "
      "155 deg exhaust duration is a figure quoted for a stock engine by "
      "owners, not a factory number, and its 38 mm outlet bore and 40 mm duct "
      "are placeholders. Measure all three before cutting anything."),
};

static const PgHelpItem it_method[] = {
    T("When the exhaust port opens, a pressure pulse runs down the chamber. "
      "The widening diffuser reflects it back as a suction wave, which helps "
      "empty the cylinder and draw fresh charge through the transfers. The "
      "narrowing baffle reflects it back as a pressure wave, which arrives "
      "as the port is closing and pushes escaping charge back into the "
      "cylinder: the plugging pulse."),
    SUB("Tuned length"),
    T("The chamber is sized so the plugging pulse gets back to the port just "
      "as it closes. The pulse travels to the middle of the baffle and back "
      "while the port is open for its duration, so the tuned length from the "
      "port face to the middle of the baffle is"),
    B("L = a x ED / (12 x N), in metres when a is the wave speed in m/s, ED "
      "the exhaust duration in degrees and N the speed in rpm."),
    B("The wave speed is the speed of sound in the exhaust gas, sqrt(1.35 x "
      "287 x T) with T in kelvin. At the default 420 C mean gas temperature "
      "that is 518 m/s, the 1700 ft/s of Jennings' classic rules."),
    T("The sections are fractions of that length, chosen by the objective, "
      "normalised so the header, diffuser, belly and half the baffle always "
      "add up to it. Diameters are multiples of the outlet bore. The "
      "diffuser's stages are of equal length with diameters in geometric "
      "progression, so each opens more steeply than the one before."),
    SUB("Objectives"),
    R("Power at a fixed rpm", "Two-stage diffuser, belly 3.0x outlet, stinger "
                              "0.62x. Timed exactly for the design speed. "
                              "The choice for a generator."),
    R("Power across a band",  "Three-stage diffuser, longer gentler baffle, "
                              "belly 2.8x. Timed 60% of the way up the band."),
    R("Economy",              "Single diffuser, belly 2.6x, stinger 0.55x. The "
                              "plugging pulse is timed 6 deg before the port "
                              "closes, to return more escaping charge."),
    R("Noise reduction",      "Gentle cones, belly 2.4x, long 0.52x stinger. "
                              "Softer reflections, less power. The design "
                              "sheet recommends a silencer of at least 20x "
                              "the swept volume, a rule of thumb."),
    SUB("The timing band"),
    T("The Profile page and the PDF plot when the suction and plugging waves "
      "reach the port against engine speed. The band quoted is the speed "
      "range over which the plugging pulse lands within 10 crank degrees of "
      "exhaust closure. It is a timing window, not a power curve."),
    N("A narrower stinger raises the pressure in the chamber and with it the "
      "piston temperature. Below about 0.45x the outlet, pipegen warns."),
};

static const PgHelpItem it_view[] = {
    T("The Design page shows the chamber in 3D beside a rough engine drawn "
      "from the bore and stroke, with the inspector on the right. Everything "
      "edited in the inspector reshapes the view as you type."),
    R("Left drag",            "Orbit around the chamber."),
    R("Right or middle drag", "Pan. Shift + left drag does the same."),
    R("Wheel",                "Zoom about the pointer."),
    R("Click",                "Select a piece, or a joint ring between pieces. "
                              "Escape clears the selection."),
    R("Double-click, F, Fit", "Frame the whole chamber."),
    R("3/4, Side, Top, End",  "Standard views; also the keys 1 to 4."),
    R("Sections, Parts, Metal","Colour by section; alternate shades per flat "
                              "part, so every joint shows; or plain metal."),
    R("Engine, Box, Seams, Grid", "Show or hide the engine, the clearance "
                              "box, the seam welds or hydroforming flanges, "
                              "and the ground grid."),
    T("Pieces that leave the clearance box, run into the engine or into the "
      "chamber, or are too short for their mitres, turn red, and the status "
      "strip counts them."),
    SUB("The inspector"),
    R("Selection",  "The selected piece — its dimensions and its section's "
                    "proportions — or the selected joint and its bend. With "
                    "nothing selected: the key figures and the warnings."),
    R("Engine",     "The project title and the engine."),
    R("Tuning",     "Objective, design speed and gas temperature."),
    R("Chamber",    "Every proportion, and a reset to the objective's preset."),
    R("Build",      "Method, material, segments, seams and sheet."),
    R("Route",      "Auto-fold, the list of bends, and where the seams sit."),
    R("Clearance",  "The optional clearance box."),
};

static const PgHelpItem it_bend[] = {
    T("A chamber for a low-speed engine is long — over three metres for the "
      "JLO at 2850 rpm — and has to be bent to fit. It bends at the joints "
      "between its pieces, so the number of segments sets how many places it "
      "can turn."),
    R("1  Select a joint", "Click on or near a joint ring in the view — within "
                           "about 10 pixels counts — or use Page Up and Page "
                           "Down to step along the chamber."),
    R("2  Bend it",        "Hold Ctrl and drag: up and down changes the angle, "
                           "sideways turns the direction round the pipe. Add "
                           "Shift to step 5 deg and 15 deg. Or type the "
                           "values in the inspector, or use [ ] and , ."),
    R("3  Twist the rest", "Turn everything past the joint, as one piece, round "
                           "the pipe coming into it: the Twist buttons on the "
                           "joint panel, or Shift with , and . for 15 deg. "
                           "Later bends keep their shape relative to each "
                           "other; only where the rest of the pipe points "
                           "changes."),
    R("4  Check it",       "Red pieces clash. The overlay and the status strip "
                           "say what with."),
    T("To wrap the pipe round something in the way — a frame member, a tank — "
      "set the clearance box round it on the Clearance tab and choose Keep "
      "out, so the box is an obstacle. Bend the pipe where it meets it, "
      "twist the rest round it, and carry on until nothing is red."),
    T("Every joint is a mitre: both pieces are cut on the plane that bisects "
      "the bend, each at half its angle. The centreline is the same length "
      "bent as straight, so the wave timing does not change."),
    T("The direction is measured round the pipe from a reference that "
      "starts pointing straight down at the port and is carried along the "
      "chamber without twisting: 0 turns down, 180 up, 90 and 270 to the "
      "sides. After a bend, down means down relative to the pipe, not the "
      "world."),
    T("The flat pattern of a mitred piece has that edge developed point by "
      "point, so it rolls up to meet its neighbour exactly. A double tick on "
      "the etch layer marks the inside of the bend at each mitred edge."),
    N("Hydroformed chambers can only bend in the plane of their edge welds; "
      "an inflated pair of halves cannot twist out of it. Bend directions "
      "are turned onto that plane. The Seam plane setting rotates it."),
    N("A piece shorter than the mitres at its two ends need is flagged: its "
      "cuts would cross. Bend less there, or use fewer segments."),
    SUB("Bend rules"),
    T("A sharp mitre reflects part of the pulse early and the flow separates "
      "at the corner, worse the fatter the pipe. So every joint is checked "
      "against rules of thumb, and one that breaks them gets an amber ring "
      "and a warning:"),
    B("no joint turns more than its section allows: 30 deg in the header, 25 "
      "in the diffuser, 20 in the belly and baffle, 15 into the stinger;"),
    B("no run of bends — a piece bent at both ends — is tighter than twice "
      "the pipe's outside diameter."),
    SUB("Auto-fold"),
    T("Route > Auto-fold searches for the best layout that keeps every rule "
      "and applies it, replacing the bends there were. A turn is always "
      "spread over several joints, never made at one."),
    R("Most compact",   "The smallest package, engine included."),
    R("Fit the box",    "Inside (or out of) the clearance box, with the most "
                        "room to spare. Needs a clearance box."),
    R("Any way, Flat, Upright",
                        "Turns in any direction, sideways only, or up and "
                        "down only. Hydroformed chambers always fold in the "
                        "plane of their seams."),
    R("Most turns",     "How many separate turns it may use."),
    R("Up to segments", "It may cut sections into more pieces, when a gentler "
                        "turn needs more joints."),
    R("Heat gap",       "Distance kept from the engine and from other parts of "
                        "the chamber, which both change the gas temperature "
                        "and so the tuning. 40 mm by default."),
    R("Box wall gap",   "Distance kept from the clearance box. 20 mm."),
    R("Quick, Normal, Thorough",
                        "How long it searches. Fold again starts somewhere "
                        "new."),
    T("Among the layouts that keep every rule it prefers the smallest (or "
      "the roomiest in the box), then the least bending — counted heavier in "
      "the belly and baffle — the fewest turns and joints, and the fewest "
      "extra pieces. The search uses a cautious model of the pipe; the "
      "winner is checked again with the full surface check, and only a "
      "layout that passes is applied. Ctrl+Z, or Revert, puts it back."),
    N("The rules are rules of thumb, not a flow simulation. A fold that keeps "
      "them should behave very close to the straight design; confirm it on "
      "the engine."),
};

static const PgHelpItem it_build[] = {
    SUB("Rolled and welded"),
    T("Each piece is unrolled to a sector of an annulus (a cone) or a "
      "rectangle (a cylinder), sized on the mean diameter — internal "
      "diameter plus one thickness — so the finished bore is the design "
      "figure. Shorter pieces are also easier to roll."),
    R("Segments",        "Pieces each section is cut into, of equal length."),
    R("Seam allowance",  "Extra width on one straight edge, for a lap or "
                         "joggled seam. Zero for a butt weld."),
    R("Seam position",   "Where round the pipe the seam welds sit."),
    R("Header, stinger from tube",
                         "Leave them off the sheet and put them on the cut "
                         "list as tube, with their mitre angles."),
    SUB("Hydroformed"),
    T("The chamber becomes two identical flat halves, welded together round "
      "their edges and inflated with water or air until each half balloons "
      "into a half-round. The flat width at every station is half the mean "
      "circumference, pi x D / 2, plus the weld margin on each side."),
    R("Weld margin",     "Flat added outside the width on each side, "
                         "consumed by the edge weld."),
    R("Seam compensation",
                         "The edge weld barely stretches. With this on, each "
                         "stretch of flat is sized so its edge is as long as "
                         "the inflated side wall it becomes — a cone comes "
                         "out shorter flat than finished — and a bend is "
                         "turned through less in the flat than in the pipe."),
    T("A half longer than the sheet is split across its width into pieces "
      "that each fit. Inflate them separately and butt-weld them."),
    T("What seam compensation corrects. The seam barely stretches while the "
      "walls between the seams balloon out, so the flat outline is not the "
      "pipe unrolled:"),
    B("Width: half the mean circumference, pi x D / 2, plus the weld margin."),
    B("Cones: on the flat half the edge steps out by pi/4 of the diameter "
      "change, on the inflated pipe by only 1/2 of it, so each cone is cut "
      "shorter flat than it is finished, with its edge as long as the wall "
      "it becomes."),
    B("Bends, which close up as the pipe inflates: across a bend the flat "
      "edge sits pi/2 times further from the centreline than the inflated "
      "seam, and pulls in as it inflates. The flat outline is cut with a "
      "gentler turn, 2 x atan((2/pi) x tan(bend / 2)): a 10° bend is cut at "
      "6.4°, 15° at 9.6°, 20° at 12.8°, 25° at 16.1°, 30° at 19.4°. A bend "
      "too tight for the halves, where the inner edge would fold over "
      "itself, gets a warning."),
    T("Not corrected for: stretch and thinning of the steel under pressure, "
      "springback when the pressure comes off, weld shrinkage and "
      "distortion, and metal next to the seam that never goes fully round — "
      "more so with a wide weld margin or low pressure. Hydroformed bends "
      "can only be made in the plane of the seam welds."),
    N("Seam compensation is a first-order geometric model, not yet checked "
      "against a real inflated piece. Inflate one bent segment, measure its "
      "finished bend angle and length against the design, and adjust before "
      "cutting a full set."),
    SUB("Sheets and layout"),
    T("Parts are turned to their flattest orientation and placed in rows on "
      "sheets of the size given, with the gap given between them. That "
      "guarantees a cuttable arrangement; a nesting program will use less "
      "material. A part larger than the sheet in every orientation gets a "
      "sheet of its own and a warning: use more segments or a larger sheet."),
};

static const PgHelpItem it_clear[] = {
    T("The clearance box is in engine coordinates: +X out of the exhaust "
      "port, +Y up the cylinder, Z along the crankshaft, with the origin on "
      "the cylinder axis at the height of the port."),
    R("Keep inside", "The box is the space available — a frame or an "
                     "enclosure. A piece that leaves it clashes."),
    R("Keep out",    "The box is an obstacle — a tank, a battery, a wheel. A "
                     "piece that enters it clashes."),
    R("Fit the box round the chamber",
                     "Sets the box to the chamber's present extent plus "
                     "50 mm, as a starting point to shrink from."),
    T("The engine drawn in the view is checked too. It is rough on purpose — "
      "a finned barrel, a head and a crankcase sized from the bore and "
      "stroke — so treat it as scale, and model the real obstruction with a "
      "keep-out box."),
};

static const PgHelpItem it_pages[] = {
    R("Design",      "The 3D view and the inspector."),
    R("Patterns",    "Each flat pattern with its dimensions, and the sheet "
                     "layouts. Clicking a part on a sheet shows it."),
    R("Profile",     "The straightened side profile to scale, and the wave "
                     "timing plot."),
    R("Export",      "Write the DXF and PDF."),
    R("Help",        "This manual. F1 opens it from anywhere."),
};

static const PgHelpItem it_export[] = {
    R("Layout DXF",     "<name>.dxf: every sheet side by side, parts "
                        "placed. AutoCAD R12, millimetres, arcs as true arcs."),
    R("Part DXFs",      "<name>-P01.dxf and so on: one part per file at the "
                        "origin, for a nesting program to place."),
    R("Design PDF",     "Design sheet, wave timing and the cut list."),
    R("Sheet layouts",  "One page per sheet, scaled to fit A4."),
    R("Templates",      "Every part at full size on a page of its own, with "
                        "a 100 mm check bar. Print at 100% on a plotter."),
    SUB("DXF layers"),
    R("CUT",    "Part outlines. The only layer to cut."),
    R("ETCH",   "Alignment marks: quarter-circumference ticks on each piece's "
                "edges, a double tick at the inside of each bend, and "
                "section joints on a hydroformed half. Mark or etch, do not "
                "cut."),
    R("LABEL",  "Part numbers, matching the cut list. Engrave or ignore."),
    R("SHEET",  "Sheet borders and titles. Never cut."),
    N("No kerf compensation is applied. Set it in the laser's CAM, which "
      "knows the machine."),
};

static const PgHelpItem it_files[] = {
    R("Projects",      "Plain text .pgp files, key = value, in the folder "
                       "you choose. Safe to keep in version control."),
    R("Settings",      "Theme, folders, recent projects and export choices, "
                       "in ~/.config/pipegen/settings.conf, or "
                       "%APPDATA%\\pipegen\\settings.conf on Windows."),
    R("Command line",  "pipegen --project FILE --export DIR NAME writes the "
                       "exports without opening a window; --autofold compact "
                       "(or box) folds it first, --save FILE keeps the result, "
                       "and --render FILE.ppm renders the 3D view. --help "
                       "lists the rest."),
    R("PIPEGEN_SCALE", "Environment variable overriding the HiDPI scale."),
    R("PIPEGEN_RENDERER", "Set to software to draw without OpenGL. The program "
      "falls back to this by itself when there is no OpenGL 3.3; About shows "
      "which is in use."),
};

#define SEC(title, intro, items) \
    { (title), (intro), (items), (int)(sizeof(items) / sizeof((items)[0])) }

static const PgHelpSection SECTIONS[] = {
    SEC("About",                       NULL, it_about),
    SEC("Starting a project",          NULL, it_start),
    SEC("The engine",                  NULL, it_engine),
    SEC("How the chamber is designed", NULL, it_method),
    SEC("The Design page",             NULL, it_view),
    SEC("Bending the chamber to fit",  NULL, it_bend),
    SEC("The clearance box",           NULL, it_clear),
    SEC("Manufacturing",               NULL, it_build),
    SEC("Pages",                       NULL, it_pages),
    SEC("Export",                      NULL, it_export),
    SEC("Files and settings",          NULL, it_files),
};

const PgHelpSection *pg_help_sections(int *n_sections)
{
    *n_sections = (int)(sizeof SECTIONS / sizeof SECTIONS[0]);
    return SECTIONS;
}

void pg_help_write_markdown(FILE *f, const char *version)
{
    fprintf(f, "# pipegen %s — manual\n\n", version);
    fprintf(f, "<!-- Generated by `pipegen --help-doc` from src/pg_help.c. "
               "Do not edit by hand. -->\n\n");

    int n = 0;
    const PgHelpSection *sec = pg_help_sections(&n);
    for (int i = 0; i < n; i++) {
        fprintf(f, "## %s\n\n", sec[i].title);
        if (sec[i].intro)
            fprintf(f, "%s\n\n", sec[i].intro);

        bool in_table = false, in_list = false;
        for (int k = 0; k < sec[i].n_items; k++) {
            const PgHelpItem *e = &sec[i].items[k];
            if (e->kind != PG_HELP_ROW && in_table) {
                fprintf(f, "\n");
                in_table = false;
            }
            if (e->kind != PG_HELP_BULLET && in_list) {
                fprintf(f, "\n");
                in_list = false;
            }
            switch (e->kind) {
            case PG_HELP_TEXT:
                fprintf(f, "%s\n\n", e->a);
                break;
            case PG_HELP_SUB:
                fprintf(f, "### %s\n\n", e->a);
                break;
            case PG_HELP_BULLET:
                fprintf(f, "- %s\n", e->a);
                in_list = true;
                break;
            case PG_HELP_ROW:
                if (!in_table) {
                    fprintf(f, "| | |\n|---|---|\n");
                    in_table = true;
                }
                fprintf(f, "| **%s** | %s |\n", e->a, e->b ? e->b : "");
                break;
            case PG_HELP_NOTE:
                fprintf(f, "> **Note:** %s\n\n", e->a);
                break;
            }
        }
        if (in_table || in_list)
            fprintf(f, "\n");
    }
}
