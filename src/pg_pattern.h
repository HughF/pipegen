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
 * pg_pattern.h — flat patterns and their layout on sheets
 *
 * Rolled and welded: every piece of the chain becomes one flat part, rolled
 * on the sheet's mean diameter (internal diameter plus one thickness) so the
 * finished bore comes out at the design figure. A piece with square ends is a
 * sector of an annulus (a cone) or a rectangle (a cylinder), with exact arcs.
 * A piece with a mitred end has that edge developed point by point from the
 * curve where the cone meets the mitre plane.
 *
 * Hydroformed: the sheet pieces become two identical flat halves whose width
 * at every station is half the mean circumference. They are welded round the
 * edge and inflated. Bends in the plane of the seams become bends in the flat
 * outline.
 *
 * Tube pieces appear on the cut list, with their mitre angles, and not on the
 * sheet.
 */
#ifndef PG_PATTERN_H
#define PG_PATTERN_H

#include "pg_project.h"
#include "pg_design.h"
#include "pg_route.h"
#include "pg_geom.h"

#define PG_MAX_PARTS      PG_MAX_PIECES
#define PG_PART_VERTS     256
#define PG_PART_MARKS     32
#define PG_PART_STATIONS  PG_MAX_PIECES + 2
#define PG_MITRE_SAMPLES  96   /* points along a developed mitre edge */

typedef enum {
    PG_PART_CONE = 0,      /* annular sector, rolled into a frustum   */
    PG_PART_WRAP,          /* rectangle, rolled into a cylinder       */
    PG_PART_PILLOW,        /* hydroformed half                        */
    PG_PART_TUBE           /* cut from tube: cut list only, no outline */
} PgPartKind;

typedef struct { double x0, y0, x1, y1; } PgLine;

typedef struct {
    PgPartKind kind;
    char   id[16];           /* "P01"                                   */
    char   name[64];
    int    qty;
    int    piece;            /* chain piece, -1 for a hydroformed half  */
    bool   mitred;           /* an end is cut on a mitre                */

    int    n;                /* closed cut outline, part-local mm       */
    PgVert v[PG_PART_VERTS];
    int    n_marks;          /* alignment marks for the etch layer      */
    PgLine marks[PG_PART_MARKS];
    double label_x, label_y; /* a point inside the part                 */

    /* figures for the drawing */
    double d_small, d_large; /* mean diameters rolled to                */
    double axial_len;
    double slant;
    double r_inner, r_outer; /* cone: radii from the apex               */
    double sweep_deg;        /* cone: included angle of the sector      */
    double flat_w, flat_l;   /* wrap and pillow: flat width and length  */
    double tube_od, tube_id, tube_len;   /* tube: longest side          */
    double cut0_deg, cut1_deg;           /* mitre tilt at each end      */

    int    n_st;             /* pillow stations: flat position, width   */
    double st_x[PG_PART_STATIONS];
    double st_hw[PG_PART_STATIONS];

    PgBox  box;
    double area_mm2;
} PgPart;

typedef struct {
    int    n;
    PgPart part[PG_MAX_PARTS];
    double sheet_area_mm2;   /* every sheet part times its quantity     */
    double mass_kg;          /* of the sheet parts                      */
    int    n_warn;
    char   warn[8][PG_WARN_LEN];
} PgPartList;

/* PgPartList is large; allocate it on the heap. Records each piece's part
 * index in the chain. */
void pg_parts_build(const PgProject *p, const PgDesign *d, PgChain *chain,
                    PgPartList *out);

#define PG_MAX_PLACE  256
#define PG_MAX_SHEETS 64

typedef struct {
    int    part;             /* index into the part list                */
    int    copy;             /* 1..qty                                  */
    int    sheet;
    double rot;              /* radians                                 */
    double x, y;             /* translation after rotation, sheet mm    */
} PgPlace;

typedef struct {
    int     n;
    PgPlace place[PG_MAX_PLACE];
    int     n_sheets;
    bool    oversize[PG_MAX_SHEETS];  /* a part bigger than the sheet  */
    int     n_oversize;
    int     n_unplaced;               /* ran out of slots               */
    double  sheet_w, sheet_h, gap;
} PgLayout;

/* Simple shelf nesting: each part turned to its flattest orientation, then
 * placed in rows. A nesting program will do better; this guarantees a
 * cuttable, non-overlapping arrangement. */
void pg_layout_build(const PgPartList *parts, const PgBuild *b, PgLayout *out);

/* A part-local point in sheet coordinates for one placement. */
void pg_place_point(const PgPlace *pl, double x, double y, double *ox, double *oy);

const char *pg_part_kind_name(PgPartKind k);

#endif /* PG_PATTERN_H */
