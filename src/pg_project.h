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
 * pg_project.h — everything a design is made from, and its file format
 *
 * A project holds inputs only. The chamber, its route through space, the flat
 * patterns and the sheet layout are all derived from it, every time, so a
 * saved file can never disagree with what it produces.
 *
 * Units throughout: millimetres, degrees, rpm, degrees Celsius. All chamber
 * diameters are internal (gas-side); the patterns add the sheet thickness.
 *
 * Engine coordinates, used by the route and the clearance box: the cylinder
 * axis is +Y (up), the crankshaft lies along Z, the exhaust port faces +X,
 * and the origin is on the cylinder axis at the height of the port.
 */
#ifndef PG_PROJECT_H
#define PG_PROJECT_H

#include <stdbool.h>
#include <stddef.h>

#define PG_PROJECT_EXT ".pgp"
#define PG_MAX_BENDS   32

typedef enum {
    PG_OBJ_POWER_RPM = 0,   /* peak power at one speed           */
    PG_OBJ_POWER_BAND,      /* power spread across a speed range */
    PG_OBJ_ECONOMY,         /* trapping efficiency, fuel         */
    PG_OBJ_NOISE,           /* gentle reflections, quiet         */
    PG_OBJ_COUNT
} PgObjective;

typedef enum {
    PG_MFG_ROLLED = 0,      /* cones rolled from sheet, seam welded      */
    PG_MFG_HYDRO,           /* two flat halves edge-welded and inflated  */
    PG_MFG_COUNT
} PgMethod;

typedef enum {
    PG_MAT_MILD = 0,
    PG_MAT_STAINLESS,
    PG_MAT_TITANIUM,
    PG_MAT_COUNT
} PgMaterial;

typedef struct {
    char   name[64];
    double bore_mm;
    double stroke_mm;
    int    cylinders;          /* one chamber per cylinder              */
    double exh_duration_deg;   /* crank degrees the exhaust port is open */
    double outlet_dia_mm;      /* exhaust port outlet / flange bore     */
    double duct_len_mm;        /* port face to flange, inside the casting */
    double port_down_deg;      /* exhaust duct angled down from horizontal */
} PgEngine;

typedef struct {
    PgObjective objective;
    double rpm;                /* design speed (fixed-speed objectives) */
    double rpm_lo, rpm_hi;     /* power-band objective                  */
    double gas_temp_c;         /* mean gas temperature in the chamber   */
} PgTuning;

/*
 * The chamber's proportions. Set from the objective's preset and editable
 * afterwards. Section lengths are fractions of the tuned length, which runs
 * from the port to the middle of the baffle cone — so header + diffuser +
 * belly + half the baffle is always 1, and the fractions are normalised to
 * keep it so.
 */
typedef struct {
    double f_header;
    double f_diffuser;
    double f_belly;
    double f_baffle;
    int    diffuser_stages;    /* 1..4; more stages, broader response   */
    double header_taper;       /* header exit diameter / outlet         */
    double belly_ratio;        /* belly diameter / outlet               */
    double stinger_ratio;      /* stinger diameter / outlet             */
    double stinger_len_dia;    /* stinger length, in stinger diameters  */
    double plug_lead_deg;      /* return pulse arrives this far before  */
                               /* exhaust closure at the design speed   */
} PgGeometry;

typedef struct {
    PgMethod   method;
    PgMaterial material;
    double thickness_mm;
    int    segments;           /* pieces each sheet section is cut into; */
                               /* the joints between them can bend       */
    bool   header_tube;        /* header cut from tube, not rolled/formed */
    bool   stinger_tube;       /* stinger cut from tube                  */
    double seam_allow_mm;      /* rolled: extra on one seam edge         */
    double hydro_margin_mm;    /* hydroformed: weld margin each side     */
    bool   hydro_seam_comp;    /* hydroformed: keep seam length true     */
    bool   etch_marks;         /* alignment marks on an etch layer       */
    double sheet_w_mm;
    double sheet_h_mm;
    double part_gap_mm;
} PgBuild;

/*
 * A bend at a joint. `at_mm` is a centreline distance from the port face;
 * the bend is applied at the joint nearest it, so bends stay where they were
 * put when a change of rpm moves every joint a little.
 *
 * `roll_deg` says which way the pipe turns, measured round the pipe from its
 * reference direction, which starts pointing straight down at the port and
 * is carried along the chamber without twisting: 0 turns down, 180 up, 90
 * and 270 to either side.
 */
typedef struct {
    double at_mm;
    double bend_deg;           /* 0..90 */
    double roll_deg;           /* 0..360 */
} PgBend;

typedef struct {
    int    n_bends;
    PgBend bends[PG_MAX_BENDS];
    double seam_deg;           /* rolled: where the seam weld sits round   */
                               /* the pipe; hydroformed: middle of a half  */
} PgRoute;

typedef struct {
    bool   enabled;
    bool   keep_inside;        /* an envelope; false: an obstacle to avoid */
    double min[3], max[3];     /* engine coordinates, mm                   */
} PgClearance;

typedef struct {
    char        title[96];
    char        notes[512];
    PgEngine    engine;
    PgTuning    tuning;
    PgGeometry  geom;
    PgBuild     build;
    PgRoute     route;
    PgClearance clear;
} PgProject;

/* The default project: a JLO L372 driving a generator at 2850 rpm. */
void pg_project_default(PgProject *p);

/* Overwrite the proportions with the preset for an objective. */
void pg_geometry_preset(PgObjective obj, PgGeometry *g);

/* Engine presets for the wizard. Index 0 is the default engine. */
int  pg_engine_preset_count(void);
const char *pg_engine_preset_name(int i);
void pg_engine_preset(int i, PgEngine *e, double *design_rpm);

const char *pg_objective_name(PgObjective o);
const char *pg_objective_blurb(PgObjective o);
const char *pg_method_name(PgMethod m);
const char *pg_material_name(PgMaterial m);
double      pg_material_density(PgMaterial m);     /* kg/m^3 */

double pg_displacement_cc(const PgEngine *e);      /* per cylinder */

/* The rpm the chamber is designed around, for any objective. */
double pg_design_rpm(const PgTuning *t);

/* Set a bend at the joint nearest `at_mm` (replacing one already there,
 * within `snap_mm`); a zero bend removes it. False if the list is full. */
bool pg_route_set_bend(PgRoute *r, double at_mm, double snap_mm,
                       double bend_deg, double roll_deg);

/* Index of the bend within `snap_mm` of `at_mm`, or -1. */
int  pg_route_find_bend(const PgRoute *r, double at_mm, double snap_mm);

/* Clamp every value into a range that produces a valid chamber. */
void pg_project_sanitise(PgProject *p);

/* Text form, the same one the file holds. Returns the length written
 * (truncated if cap is too small, like snprintf). */
size_t pg_project_write(const PgProject *p, char *buf, size_t cap);

/* Parse a text form produced by pg_project_write. Unknown keys are ignored
 * and missing ones keep their defaults, so older files still open. */
bool pg_project_parse(PgProject *p, const char *text, char *err, size_t errcap);

bool pg_project_save(const PgProject *p, const char *path, char *err, size_t errcap);
bool pg_project_load(PgProject *p, const char *path, char *err, size_t errcap);

#endif /* PG_PROJECT_H */
