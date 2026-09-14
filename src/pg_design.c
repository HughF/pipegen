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
#include "pg_design.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

/* Exhaust gas as a perfect gas: gamma and the specific gas constant sit close
 * to air's at chamber temperatures. At 420 C this gives 518 m/s, the 1700 ft/s
 * Jennings built his rules on. */
#define GAMMA   1.35
#define R_GAS   287.0

double pg_wave_speed(double temp_c)
{
    return sqrt(GAMMA * R_GAS * (temp_c + 273.15));
}

void pg_design_add_warning(PgDesign *d, const char *fmt, ...)
{
    if (d->n_warn >= PG_MAX_WARN)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->warn[d->n_warn], PG_WARN_LEN, fmt, ap);
    va_end(ap);
    d->n_warn++;
}

double pg_section_half_angle(const PgSection *s)
{
    if (s->len <= 0.0)
        return 90.0;
    return atan(fabs(s->d1 - s->d0) / 2.0 / s->len) * 180.0 / M_PI;
}

double pg_return_angle(const PgDesign *d, double dist_mm, double rpm)
{
    double t = 2.0 * dist_mm / 1000.0 / d->wave_speed;    /* seconds */
    return d->epo_deg + 6.0 * rpm * t;                    /* 6N deg/s */
}

double pg_design_diameter_at(const PgDesign *d, double x, int *sec)
{
    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        if (x >= s->x0 && x <= s->x0 + s->len) {
            if (sec) *sec = i;
            double u = s->len > 0.0 ? (x - s->x0) / s->len : 0.0;
            return s->d0 + (s->d1 - s->d0) * u;
        }
    }
    if (sec) *sec = -1;
    return 0.0;
}

static void add_section(PgDesign *d, PgSectionKind kind, int stage,
                        const char *name, double len, double d0, double d1)
{
    if (d->n_sec >= PG_MAX_SECTIONS)
        return;
    PgSection *s = &d->sec[d->n_sec];
    double x0 = d->n_sec ? d->sec[d->n_sec - 1].x0 + d->sec[d->n_sec - 1].len
                         : 0.0;
    s->kind = kind;
    s->stage = stage;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->x0 = x0;
    s->len = len;
    s->d0 = d0;
    s->d1 = d1;
    d->n_sec++;
}

void pg_design_compute(const PgProject *p, PgDesign *d)
{
    memset(d, 0, sizeof *d);

    const PgEngine   *e = &p->engine;
    const PgTuning   *t = &p->tuning;
    const PgGeometry *g = &p->geom;

    double ed = e->exh_duration_deg;

    d->displacement_cc = pg_displacement_cc(e);
    d->design_rpm      = pg_design_rpm(t);
    d->wave_speed      = pg_wave_speed(t->gas_temp_c);
    d->epo_deg         = 180.0 - ed / 2.0;
    d->epc_deg         = 180.0 + ed / 2.0;

    /*
     * Tuned speed. With no lead the plugging pulse meets port closure at the
     * design rpm. A lead of L degrees wants it back L degrees early, which is
     * the same as a pipe tuned for a proportionally higher speed, since the
     * return angle grows linearly with rpm.
     */
    double lead = g->plug_lead_deg;
    if (lead > ed * 0.5)
        lead = ed * 0.5;
    d->tuned_rpm = d->design_rpm * ed / (ed - lead);

    /* The pulse travels to the reflection point and back while the port is
     * open: 2 L = a * ED / (6 N)  =>  L = a * ED / (12 N). */
    d->tuned_len = d->wave_speed * ed / (12.0 * d->tuned_rpm) * 1000.0;

    /* Normalise the fractions so the baffle's middle stays at the tuned
     * length whatever the user typed. */
    double sum = g->f_header + g->f_diffuser + g->f_belly + g->f_baffle / 2.0;
    if (sum <= 0.0)
        sum = 1.0;
    double lt = d->tuned_len;
    double l_header  = lt * g->f_header / sum;
    double l_diffuse = lt * g->f_diffuser / sum;
    double l_belly   = lt * g->f_belly / sum;
    double l_baffle  = lt * g->f_baffle / sum;

    d->d_outlet  = e->outlet_dia_mm;
    d->d_belly   = e->outlet_dia_mm * g->belly_ratio;
    d->d_stinger = e->outlet_dia_mm * g->stinger_ratio;
    double d_hdr_exit = e->outlet_dia_mm * g->header_taper;

    if (d->d_belly <= d_hdr_exit * 1.05) {
        pg_design_add_warning(d,
            "Belly (%.0f mm) is barely wider than the header exit (%.0f mm): "
            "the diffuser will do almost nothing. Raise the belly ratio or "
            "lower the header taper.", d->d_belly, d_hdr_exit);
    }

    add_section(d, PG_SEC_HEADER, 0, "Header", l_header,
                e->outlet_dia_mm, d_hdr_exit);

    /* Diffuser stages of equal length with diameters in geometric
     * progression: each stage opens out more steeply than the last, an
     * approximation to the exponential horn that spreads the suction wave. */
    int n = g->diffuser_stages;
    double ratio = d->d_belly / d_hdr_exit;
    for (int i = 0; i < n; i++) {
        char name[32];
        if (n == 1)
            snprintf(name, sizeof name, "Diffuser");
        else
            snprintf(name, sizeof name, "Diffuser %d", i + 1);
        double da = d_hdr_exit * pow(ratio, (double)i / n);
        double db = d_hdr_exit * pow(ratio, (double)(i + 1) / n);
        add_section(d, PG_SEC_DIFFUSER, i + 1, name, l_diffuse / n, da, db);
    }

    if (l_belly > 0.5)
        add_section(d, PG_SEC_BELLY, 0, "Belly", l_belly,
                    d->d_belly, d->d_belly);
    add_section(d, PG_SEC_BAFFLE, 0, "Baffle", l_baffle,
                d->d_belly, d->d_stinger);
    add_section(d, PG_SEC_STINGER, 0, "Stinger",
                d->d_stinger * g->stinger_len_dia,
                d->d_stinger, d->d_stinger);

    const PgSection *last = &d->sec[d->n_sec - 1];
    d->total_len = last->x0 + last->len;

    d->suction_dist = l_header + l_diffuse / 2.0;
    d->plug_dist    = l_header + l_diffuse + l_belly + l_baffle / 2.0;

    /* Speeds at which the plugging pulse lands within the window either side
     * of port closure. Return angle is linear in rpm, so solve directly. */
    double k = 12.0 * d->plug_dist / 1000.0 / d->wave_speed; /* deg per rpm */
    d->band_lo = (ed - PG_TIMING_WINDOW_DEG) / k;
    d->band_hi = (ed + PG_TIMING_WINDOW_DEG) / k;

    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        double a = pg_section_half_angle(s);
        if (s->kind == PG_SEC_DIFFUSER && a > d->max_diffuser_angle)
            d->max_diffuser_angle = a;
        if (s->kind == PG_SEC_BAFFLE)
            d->baffle_angle = a;
        /* frustum volume */
        double r0 = s->d0 / 2.0, r1 = s->d1 / 2.0;
        d->volume_litres += M_PI * s->len / 3.0 * (r0 * r0 + r0 * r1 + r1 * r1)
                          / 1e6;
    }

    if (t->objective == PG_OBJ_NOISE)
        d->silencer_litres = 20.0 * d->displacement_cc / 1000.0;

    /* ---- checks ---- */

    if (ed < 110.0 || ed > 210.0)
        pg_design_add_warning(d,
            "Exhaust duration %.0f deg is outside the 110-210 deg range these "
            "rules come from. Check the figure.", ed);

    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        double a = pg_section_half_angle(s);
        if (s->kind == PG_SEC_DIFFUSER && a > 8.0)
            pg_design_add_warning(d,
                "%s half-angle is %.1f deg. Above about 8 deg the flow can "
                "separate from the wall and the suction wave weakens; add a "
                "stage or lengthen the diffuser.", s->name, a);
        if (s->kind == PG_SEC_BAFFLE && a > 20.0)
            pg_design_add_warning(d,
                "Baffle half-angle is %.1f deg — a very hard reflection. "
                "Expect a narrow band and high piston temperatures.", a);
    }

    if (g->belly_ratio < 2.0 || g->belly_ratio > 3.5)
        pg_design_add_warning(d,
            "Belly ratio %.2f is outside the usual 2.0-3.5.", g->belly_ratio);

    if (g->stinger_ratio < 0.45 || g->stinger_ratio > 0.70)
        pg_design_add_warning(d,
            "Stinger ratio %.2f is outside the usual 0.45-0.70. Too small "
            "overheats the piston; too large bleeds the plugging pulse.",
            g->stinger_ratio);

    if (t->objective == PG_OBJ_POWER_BAND && t->rpm_hi <= t->rpm_lo)
        pg_design_add_warning(d,
            "The band's top speed is not above its bottom speed.");

    if (t->gas_temp_c < 250.0 || t->gas_temp_c > 700.0)
        pg_design_add_warning(d,
            "Mean gas temperature %.0f C is unusual for a chamber (250-700 C). "
            "Every length scales with the square root of it.", t->gas_temp_c);

    if (d->total_len > 2000.0)
        pg_design_add_warning(d,
            "The chamber is %.2f m long, which is normal at %.0f rpm. Cut the "
            "cones into segments so the chamber can be bent to fit the frame.",
            d->total_len / 1000.0, d->design_rpm);
}
