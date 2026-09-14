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
 * pg_design.h — from engine and objective to chamber dimensions
 *
 * The method is wave timing, in the tradition of Jennings and Blair: the
 * pressure pulse leaves the port when it opens, reflects off the diffuser as
 * a suction wave and off the baffle as a plugging wave, and the chamber is
 * sized so the plugging wave gets back to the port as it closes at the design
 * speed. It gives a sound starting geometry. It is not a gas-dynamic
 * simulation, and the UI says so.
 */
#ifndef PG_DESIGN_H
#define PG_DESIGN_H

#include "pg_project.h"

#define PG_MAX_SECTIONS 12
#define PG_MAX_WARN     12
#define PG_WARN_LEN     200

/* The plugging pulse is counted "on time" within this many crank degrees of
 * exhaust closure, which is what the reported rpm band means. */
#define PG_TIMING_WINDOW_DEG 10.0

typedef enum {
    PG_SEC_HEADER = 0,
    PG_SEC_DIFFUSER,
    PG_SEC_BELLY,
    PG_SEC_BAFFLE,
    PG_SEC_STINGER
} PgSectionKind;

typedef struct {
    PgSectionKind kind;
    int    stage;           /* diffuser stage, 1-based; 0 otherwise    */
    char   name[32];
    double x0;              /* start, mm from the port face            */
    double len;             /* axial length, mm                        */
    double d0, d1;          /* internal diameter at start and end, mm  */
} PgSection;

typedef struct {
    double displacement_cc;  /* per cylinder                            */
    double design_rpm;
    double tuned_rpm;        /* plugging pulse exactly at port closure  */
    double wave_speed;       /* m/s                                     */
    double tuned_len;        /* mm, port face to middle of the baffle   */
    double total_len;        /* mm, port face to stinger exit           */
    double epo_deg;          /* exhaust opens, degrees after TDC        */
    double epc_deg;          /* exhaust closes                          */
    double d_outlet, d_belly, d_stinger;
    double suction_dist;     /* mm, port to middle of the diffuser      */
    double plug_dist;        /* mm, port to middle of the baffle        */
    double band_lo, band_hi; /* rpm with the plugging pulse on time     */
    double silencer_litres;  /* noise objective only, else 0            */
    double max_diffuser_angle; /* steepest diffuser half-angle, degrees */
    double baffle_angle;       /* baffle half-angle, degrees            */
    double volume_litres;      /* internal volume of the chamber        */

    int       n_sec;
    PgSection sec[PG_MAX_SECTIONS];

    int  n_warn;
    char warn[PG_MAX_WARN][PG_WARN_LEN];
} PgDesign;

/* Speed of sound in exhaust gas at `temp_c`, m/s. */
double pg_wave_speed(double temp_c);

void pg_design_compute(const PgProject *p, PgDesign *d);

/* Crank angle (degrees after TDC) at which a wave that left the port when it
 * opened gets back, having reflected `dist_mm` down the chamber, at `rpm`. */
double pg_return_angle(const PgDesign *d, double dist_mm, double rpm);

/* Half-angle of a section's wall to the axis, degrees. */
double pg_section_half_angle(const PgSection *s);

/* Internal diameter at `x` mm from the port; the section index goes to
 * *sec if non-NULL (-1 outside the chamber). */
double pg_design_diameter_at(const PgDesign *d, double x, int *sec);

void pg_design_add_warning(PgDesign *d, const char *fmt, ...);

#endif /* PG_DESIGN_H */
