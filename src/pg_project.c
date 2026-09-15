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
#include "pg_project.h"
#include "plat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define FORMAT_VERSION 1
#define MAGIC "# pipegen project"

/* ------------------------------------------------------------------ */
/* Presets                                                             */
/* ------------------------------------------------------------------ */

/*
 * The JLO L372's bore and stroke are the published 80 x 74 mm (372 cc). The
 * exhaust duration, outlet bore and duct length are NOT from a JLO document:
 * 155 degrees is a figure quoted for a stock 372 on an owners' forum, and 38
 * and 40 mm are placeholders. All three are flagged in the wizard as values
 * to measure on the engine in hand, because the tuned length is directly
 * proportional to the first and the whole chamber scales with the second.
 */
typedef struct {
    const char *name;
    PgEngine    engine;
    double      rpm;
} EnginePreset;

static const EnginePreset ENGINES[] = {
    { "JLO L372 (generator)",
      { "JLO L372", 80.0, 74.0, 1, 155.0, 38.0, 40.0, 0.0 }, 2850.0 },
};

int pg_engine_preset_count(void)
{
    return (int)(sizeof ENGINES / sizeof ENGINES[0]);
}

const char *pg_engine_preset_name(int i)
{
    if (i < 0 || i >= pg_engine_preset_count())
        return "";
    return ENGINES[i].name;
}

void pg_engine_preset(int i, PgEngine *e, double *design_rpm)
{
    if (i < 0 || i >= pg_engine_preset_count())
        return;
    *e = ENGINES[i].engine;
    if (design_rpm)
        *design_rpm = ENGINES[i].rpm;
}

/*
 * Proportions per objective. In every row header + diffuser + belly + half
 * the baffle = 1, which puts the middle of the baffle at the tuned length.
 */
void pg_geometry_preset(PgObjective obj, PgGeometry *g)
{
    switch (obj) {
    default:
    case PG_OBJ_POWER_RPM:
        *g = (PgGeometry){ .f_header = 0.14, .f_diffuser = 0.46,
                           .f_belly = 0.16, .f_baffle = 0.48,
                           .diffuser_stages = 2, .header_taper = 1.05,
                           .belly_ratio = 3.0, .stinger_ratio = 0.62,
                           .stinger_len_dia = 12.0, .plug_lead_deg = 0.0 };
        break;
    case PG_OBJ_POWER_BAND:
        *g = (PgGeometry){ .f_header = 0.12, .f_diffuser = 0.48,
                           .f_belly = 0.12, .f_baffle = 0.56,
                           .diffuser_stages = 3, .header_taper = 1.05,
                           .belly_ratio = 2.8, .stinger_ratio = 0.60,
                           .stinger_len_dia = 12.0, .plug_lead_deg = 0.0 };
        break;
    case PG_OBJ_ECONOMY:
        *g = (PgGeometry){ .f_header = 0.16, .f_diffuser = 0.40,
                           .f_belly = 0.20, .f_baffle = 0.48,
                           .diffuser_stages = 1, .header_taper = 1.0,
                           .belly_ratio = 2.6, .stinger_ratio = 0.55,
                           .stinger_len_dia = 14.0, .plug_lead_deg = 6.0 };
        break;
    case PG_OBJ_NOISE:
        *g = (PgGeometry){ .f_header = 0.14, .f_diffuser = 0.50,
                           .f_belly = 0.14, .f_baffle = 0.44,
                           .diffuser_stages = 2, .header_taper = 1.0,
                           .belly_ratio = 2.4, .stinger_ratio = 0.52,
                           .stinger_len_dia = 16.0, .plug_lead_deg = 0.0 };
        break;
    }
}

void pg_project_default(PgProject *p)
{
    memset(p, 0, sizeof *p);
    snprintf(p->title, sizeof p->title, "JLO L372 generator chamber");

    pg_engine_preset(0, &p->engine, &p->tuning.rpm);
    p->tuning.objective  = PG_OBJ_POWER_RPM;
    p->tuning.rpm_lo     = 2500.0;
    p->tuning.rpm_hi     = 3200.0;
    p->tuning.gas_temp_c = 420.0;

    pg_geometry_preset(p->tuning.objective, &p->geom);

    p->build.method          = PG_MFG_ROLLED;
    p->build.material        = PG_MAT_MILD;
    p->build.thickness_mm    = 1.0;
    p->build.segments        = 2;
    p->build.header_tube     = false;
    p->build.stinger_tube    = true;
    p->build.seam_allow_mm   = 0.0;
    p->build.hydro_margin_mm = 3.0;
    p->build.hydro_seam_comp = true;
    p->build.etch_marks      = true;
    p->build.sheet_w_mm      = 2500.0;
    p->build.sheet_h_mm      = 1250.0;
    p->build.part_gap_mm     = 10.0;

    p->route.n_bends  = 0;
    p->route.seam_deg = 0.0;

    /* A generator frame's worth of space around the engine, off by default. */
    p->clear.enabled     = false;
    p->clear.keep_inside = true;
    p->clear.min[0] = -300.0; p->clear.min[1] = -450.0; p->clear.min[2] = -400.0;
    p->clear.max[0] = 1100.0; p->clear.max[1] =  350.0; p->clear.max[2] =  400.0;
}

const char *pg_objective_name(PgObjective o)
{
    switch (o) {
    case PG_OBJ_POWER_RPM:  return "Power at a fixed rpm";
    case PG_OBJ_POWER_BAND: return "Power across a band";
    case PG_OBJ_ECONOMY:    return "Economy";
    case PG_OBJ_NOISE:      return "Noise reduction";
    default:                return "?";
    }
}

const char *pg_objective_blurb(PgObjective o)
{
    switch (o) {
    case PG_OBJ_POWER_RPM:
        return "Peak power at one speed. Strong diffuser suction and a hard "
               "plugging pulse, both timed for the design rpm. The right choice "
               "for a generator or anything else that runs at a fixed speed.";
    case PG_OBJ_POWER_BAND:
        return "Power across a speed range. A three-stage diffuser and a longer, "
               "gentler baffle spread the response; the timing is set 60% of the "
               "way up the band, because response falls away faster above the "
               "tuned speed than below it.";
    case PG_OBJ_ECONOMY:
        return "Fuel economy. A smaller belly and a narrower stinger lean on the "
               "plugging pulse, timed to arrive slightly before the port closes, "
               "to push escaping fresh charge back into the cylinder.";
    case PG_OBJ_NOISE:
        return "Quiet running. Gentle cones and a narrow belly soften the "
               "reflections, and a long, narrow stinger feeds a silencer. Expect "
               "less power than the other objectives.";
    default:
        return "";
    }
}

const char *pg_method_name(PgMethod m)
{
    switch (m) {
    case PG_MFG_ROLLED: return "Rolled and welded";
    case PG_MFG_HYDRO:  return "Hydroformed";
    default:            return "?";
    }
}

const char *pg_material_name(PgMaterial m)
{
    switch (m) {
    case PG_MAT_MILD:      return "Mild steel";
    case PG_MAT_STAINLESS: return "Stainless steel (304)";
    case PG_MAT_TITANIUM:  return "Titanium (grade 2)";
    default:               return "?";
    }
}

double pg_material_density(PgMaterial m)
{
    switch (m) {
    case PG_MAT_STAINLESS: return 8000.0;
    case PG_MAT_TITANIUM:  return 4510.0;
    case PG_MAT_MILD:
    default:               return 7850.0;
    }
}

double pg_displacement_cc(const PgEngine *e)
{
    return M_PI / 4.0 * e->bore_mm * e->bore_mm * e->stroke_mm / 1000.0;
}

double pg_design_rpm(const PgTuning *t)
{
    if (t->objective == PG_OBJ_POWER_BAND) {
        double lo = t->rpm_lo, hi = t->rpm_hi;
        if (hi < lo) { double s = lo; lo = hi; hi = s; }
        return lo + 0.6 * (hi - lo);
    }
    return t->rpm;
}

/* ------------------------------------------------------------------ */
/* Bends                                                               */
/* ------------------------------------------------------------------ */

int pg_route_find_bend(const PgRoute *r, double at_mm, double snap_mm)
{
    int best = -1;
    double best_d = snap_mm;
    for (int i = 0; i < r->n_bends; i++) {
        double d = fabs(r->bends[i].at_mm - at_mm);
        if (d <= best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

bool pg_route_set_bend(PgRoute *r, double at_mm, double snap_mm,
                       double bend_deg, double roll_deg)
{
    int i = pg_route_find_bend(r, at_mm, snap_mm);

    if (fabs(bend_deg) < 1e-9) {
        if (i >= 0) {
            memmove(&r->bends[i], &r->bends[i + 1],
                    (size_t)(r->n_bends - i - 1) * sizeof r->bends[0]);
            r->n_bends--;
        }
        return true;
    }
    if (i < 0) {
        if (r->n_bends >= PG_MAX_BENDS)
            return false;
        i = r->n_bends++;
    }
    r->bends[i].at_mm = at_mm;
    r->bends[i].bend_deg = bend_deg;
    r->bends[i].roll_deg = roll_deg;
    return true;
}

/* ------------------------------------------------------------------ */
/* Text form                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;          /* what would have been written, like snprintf */
} Out;

static void outf(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    size_t room = (o->len < o->cap) ? o->cap - o->len : 0;
    int n = vsnprintf(room ? o->buf + o->len : NULL, room, fmt, ap);
    va_end(ap);
    if (n > 0)
        o->len += (size_t)n;
}

/* Strings are one line in the file: backslash and newline are escaped. */
static void out_str(Out *o, const char *key, const char *s)
{
    outf(o, "%s=", key);
    for (; *s; s++) {
        if (*s == '\\')      outf(o, "\\\\");
        else if (*s == '\n') outf(o, "\\n");
        else if (*s == '\r') continue;
        else                 outf(o, "%c", *s);
    }
    outf(o, "\n");
}

/* Ten significant figures: %.6g turned a typed 2.345678 into 2.34568, so a
 * save and reload quietly changed the design. */
static void out_num(Out *o, const char *key, double v)
{
    outf(o, "%s=%.10g\n", key, v);
}

static void out_vec(Out *o, const char *key, const double *v)
{
    outf(o, "%s=%.10g,%.10g,%.10g\n", key, v[0], v[1], v[2]);
}

size_t pg_project_write(const PgProject *p, char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    if (cap)
        buf[0] = '\0';

    outf(&o, MAGIC "\n");
    outf(&o, "format=%d\n", FORMAT_VERSION);
    out_str(&o, "title", p->title);
    out_str(&o, "notes", p->notes);

    out_str(&o, "engine.name", p->engine.name);
    out_num(&o, "engine.bore_mm", p->engine.bore_mm);
    out_num(&o, "engine.stroke_mm", p->engine.stroke_mm);
    outf(&o, "engine.cylinders=%d\n", p->engine.cylinders);
    out_num(&o, "engine.exh_duration_deg", p->engine.exh_duration_deg);
    out_num(&o, "engine.outlet_dia_mm", p->engine.outlet_dia_mm);
    out_num(&o, "engine.duct_len_mm", p->engine.duct_len_mm);
    out_num(&o, "engine.port_down_deg", p->engine.port_down_deg);

    outf(&o, "tuning.objective=%d\n", (int)p->tuning.objective);
    out_num(&o, "tuning.rpm", p->tuning.rpm);
    out_num(&o, "tuning.rpm_lo", p->tuning.rpm_lo);
    out_num(&o, "tuning.rpm_hi", p->tuning.rpm_hi);
    out_num(&o, "tuning.gas_temp_c", p->tuning.gas_temp_c);

    out_num(&o, "geom.f_header", p->geom.f_header);
    out_num(&o, "geom.f_diffuser", p->geom.f_diffuser);
    out_num(&o, "geom.f_belly", p->geom.f_belly);
    out_num(&o, "geom.f_baffle", p->geom.f_baffle);
    outf(&o, "geom.diffuser_stages=%d\n", p->geom.diffuser_stages);
    out_num(&o, "geom.header_taper", p->geom.header_taper);
    out_num(&o, "geom.belly_ratio", p->geom.belly_ratio);
    out_num(&o, "geom.stinger_ratio", p->geom.stinger_ratio);
    out_num(&o, "geom.stinger_len_dia", p->geom.stinger_len_dia);
    out_num(&o, "geom.plug_lead_deg", p->geom.plug_lead_deg);

    outf(&o, "build.method=%d\n", (int)p->build.method);
    outf(&o, "build.material=%d\n", (int)p->build.material);
    out_num(&o, "build.thickness_mm", p->build.thickness_mm);
    outf(&o, "build.segments=%d\n", p->build.segments);
    outf(&o, "build.header_tube=%d\n", p->build.header_tube ? 1 : 0);
    outf(&o, "build.stinger_tube=%d\n", p->build.stinger_tube ? 1 : 0);
    out_num(&o, "build.seam_allow_mm", p->build.seam_allow_mm);
    out_num(&o, "build.hydro_margin_mm", p->build.hydro_margin_mm);
    outf(&o, "build.hydro_seam_comp=%d\n", p->build.hydro_seam_comp ? 1 : 0);
    outf(&o, "build.etch_marks=%d\n", p->build.etch_marks ? 1 : 0);
    out_num(&o, "build.sheet_w_mm", p->build.sheet_w_mm);
    out_num(&o, "build.sheet_h_mm", p->build.sheet_h_mm);
    out_num(&o, "build.part_gap_mm", p->build.part_gap_mm);

    out_num(&o, "route.seam_deg", p->route.seam_deg);
    for (int i = 0; i < p->route.n_bends; i++)
        outf(&o, "route.bend=%.10g,%.10g,%.10g\n", p->route.bends[i].at_mm,
             p->route.bends[i].bend_deg, p->route.bends[i].roll_deg);

    outf(&o, "clear.enabled=%d\n", p->clear.enabled ? 1 : 0);
    outf(&o, "clear.keep_inside=%d\n", p->clear.keep_inside ? 1 : 0);
    out_vec(&o, "clear.min", p->clear.min);
    out_vec(&o, "clear.max", p->clear.max);

    return o.len;
}

static void unescape_into(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src && n + 1 < cap; src++) {
        if (*src == '\\' && src[1]) {
            src++;
            dst[n++] = (*src == 'n') ? '\n' : *src;
        } else {
            dst[n++] = *src;
        }
    }
    dst[n] = '\0';
}

static int parse_triple(const char *v, double out[3])
{
    char *end;
    int n = 0;
    const char *s = v;
    while (n < 3) {
        out[n] = strtod(s, &end);
        if (end == s)
            break;
        n++;
        s = end;
        while (*s == ',' || *s == ' ')
            s++;
    }
    return n;
}

static double clampd(double v, double lo, double hi)
{
    if (!isfinite(v)) return lo;
    return v < lo ? lo : v > hi ? hi : v;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Keep a hand-edited or damaged file from producing nonsense geometry. The
 * ranges are generous: they catch typing errors, not unusual engines. */
void pg_project_sanitise(PgProject *p)
{
    PgEngine *e = &p->engine;
    e->bore_mm          = clampd(e->bore_mm, 10.0, 300.0);
    e->stroke_mm        = clampd(e->stroke_mm, 10.0, 300.0);
    e->cylinders        = clampi(e->cylinders, 1, 8);
    e->exh_duration_deg = clampd(e->exh_duration_deg, 60.0, 260.0);
    e->outlet_dia_mm    = clampd(e->outlet_dia_mm, 8.0, 200.0);
    e->duct_len_mm      = clampd(e->duct_len_mm, 0.0, 500.0);
    e->port_down_deg    = clampd(e->port_down_deg, -60.0, 60.0);

    PgTuning *t = &p->tuning;
    t->objective  = (PgObjective)clampi((int)t->objective, 0, PG_OBJ_COUNT - 1);
    t->rpm        = clampd(t->rpm, 300.0, 30000.0);
    t->rpm_lo     = clampd(t->rpm_lo, 300.0, 30000.0);
    t->rpm_hi     = clampd(t->rpm_hi, 300.0, 30000.0);
    t->gas_temp_c = clampd(t->gas_temp_c, 50.0, 1100.0);

    PgGeometry *g = &p->geom;
    g->f_header        = clampd(g->f_header, 0.01, 0.9);
    g->f_diffuser      = clampd(g->f_diffuser, 0.05, 0.9);
    g->f_belly         = clampd(g->f_belly, 0.0, 0.9);
    g->f_baffle        = clampd(g->f_baffle, 0.05, 1.5);
    g->diffuser_stages = clampi(g->diffuser_stages, 1, 4);
    g->header_taper    = clampd(g->header_taper, 1.0, 2.0);
    g->belly_ratio     = clampd(g->belly_ratio, 1.2, 6.0);
    g->stinger_ratio   = clampd(g->stinger_ratio, 0.2, 1.2);
    g->stinger_len_dia = clampd(g->stinger_len_dia, 1.0, 40.0);
    g->plug_lead_deg   = clampd(g->plug_lead_deg, -30.0, 30.0);

    PgBuild *b = &p->build;
    b->method          = (PgMethod)clampi((int)b->method, 0, PG_MFG_COUNT - 1);
    b->material        = (PgMaterial)clampi((int)b->material, 0, PG_MAT_COUNT - 1);
    b->thickness_mm    = clampd(b->thickness_mm, 0.3, 6.0);
    b->segments        = clampi(b->segments, 1, 8);
    b->seam_allow_mm   = clampd(b->seam_allow_mm, 0.0, 30.0);
    b->hydro_margin_mm = clampd(b->hydro_margin_mm, 0.0, 30.0);
    b->sheet_w_mm      = clampd(b->sheet_w_mm, 100.0, 10000.0);
    b->sheet_h_mm      = clampd(b->sheet_h_mm, 100.0, 10000.0);
    b->part_gap_mm     = clampd(b->part_gap_mm, 0.0, 100.0);

    PgRoute *r = &p->route;
    r->seam_deg = fmod(clampd(r->seam_deg, -3600.0, 3600.0), 360.0);
    if (r->seam_deg < 0.0)
        r->seam_deg += 360.0;
    r->n_bends = clampi(r->n_bends, 0, PG_MAX_BENDS);
    for (int i = 0; i < r->n_bends; i++) {
        PgBend *bd = &r->bends[i];
        bd->at_mm = clampd(bd->at_mm, 0.0, 100000.0);
        bd->bend_deg = clampd(bd->bend_deg, 0.0, 90.0);
        bd->roll_deg = fmod(clampd(bd->roll_deg, -3600.0, 3600.0), 360.0);
        if (bd->roll_deg < 0.0)
            bd->roll_deg += 360.0;
    }

    PgClearance *c = &p->clear;
    for (int i = 0; i < 3; i++) {
        c->min[i] = clampd(c->min[i], -20000.0, 20000.0);
        c->max[i] = clampd(c->max[i], -20000.0, 20000.0);
        if (c->max[i] < c->min[i]) {
            double s = c->min[i];
            c->min[i] = c->max[i];
            c->max[i] = s;
        }
    }
}

bool pg_project_parse(PgProject *p, const char *text, char *err, size_t errcap)
{
    pg_project_default(p);

    if (strncmp(text, MAGIC, strlen(MAGIC)) != 0) {
        snprintf(err, errcap, "Not a pipegen project file.");
        return false;
    }

    const char *s = text;
    while (*s) {
        const char *eol = strchr(s, '\n');
        size_t n = eol ? (size_t)(eol - s) : strlen(s);

        char line[1024];
        if (n >= sizeof line)
            n = sizeof line - 1;
        memcpy(line, s, n);
        line[n] = '\0';
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' '))
            line[--n] = '\0';
        s = eol ? eol + 1 : s + strlen(s);

        if (line[0] == '#' || line[0] == '\0')
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        double d = atof(v);
        int    i = atoi(v);

#define NUM(key, field) else if (strcmp(k, key) == 0) (field) = d
#define INT(key, field) else if (strcmp(k, key) == 0) (field) = i
#define BOOL(key, field) else if (strcmp(k, key) == 0) (field) = (i != 0)
        if (strcmp(k, "format") == 0) {
            if (i > FORMAT_VERSION)
                snprintf(err, errcap,
                         "Written by a newer pipegen (format %d); some settings "
                         "may not have loaded.", i);
        }
        else if (strcmp(k, "title") == 0)
            unescape_into(p->title, sizeof p->title, v);
        else if (strcmp(k, "notes") == 0)
            unescape_into(p->notes, sizeof p->notes, v);
        else if (strcmp(k, "engine.name") == 0)
            unescape_into(p->engine.name, sizeof p->engine.name, v);
        NUM("engine.bore_mm", p->engine.bore_mm);
        NUM("engine.stroke_mm", p->engine.stroke_mm);
        INT("engine.cylinders", p->engine.cylinders);
        NUM("engine.exh_duration_deg", p->engine.exh_duration_deg);
        NUM("engine.outlet_dia_mm", p->engine.outlet_dia_mm);
        NUM("engine.duct_len_mm", p->engine.duct_len_mm);
        NUM("engine.port_down_deg", p->engine.port_down_deg);
        INT("tuning.objective", p->tuning.objective);
        NUM("tuning.rpm", p->tuning.rpm);
        NUM("tuning.rpm_lo", p->tuning.rpm_lo);
        NUM("tuning.rpm_hi", p->tuning.rpm_hi);
        NUM("tuning.gas_temp_c", p->tuning.gas_temp_c);
        NUM("geom.f_header", p->geom.f_header);
        NUM("geom.f_diffuser", p->geom.f_diffuser);
        NUM("geom.f_belly", p->geom.f_belly);
        NUM("geom.f_baffle", p->geom.f_baffle);
        INT("geom.diffuser_stages", p->geom.diffuser_stages);
        NUM("geom.header_taper", p->geom.header_taper);
        NUM("geom.belly_ratio", p->geom.belly_ratio);
        NUM("geom.stinger_ratio", p->geom.stinger_ratio);
        NUM("geom.stinger_len_dia", p->geom.stinger_len_dia);
        NUM("geom.plug_lead_deg", p->geom.plug_lead_deg);
        INT("build.method", p->build.method);
        INT("build.material", p->build.material);
        NUM("build.thickness_mm", p->build.thickness_mm);
        INT("build.segments", p->build.segments);
        BOOL("build.header_tube", p->build.header_tube);
        BOOL("build.stinger_tube", p->build.stinger_tube);
        NUM("build.seam_allow_mm", p->build.seam_allow_mm);
        NUM("build.hydro_margin_mm", p->build.hydro_margin_mm);
        BOOL("build.hydro_seam_comp", p->build.hydro_seam_comp);
        BOOL("build.etch_marks", p->build.etch_marks);
        NUM("build.sheet_w_mm", p->build.sheet_w_mm);
        NUM("build.sheet_h_mm", p->build.sheet_h_mm);
        NUM("build.part_gap_mm", p->build.part_gap_mm);
        NUM("route.seam_deg", p->route.seam_deg);
        else if (strcmp(k, "route.bend") == 0) {
            double t3[3];
            if (parse_triple(v, t3) == 3 && p->route.n_bends < PG_MAX_BENDS) {
                PgBend *b = &p->route.bends[p->route.n_bends++];
                b->at_mm = t3[0];
                b->bend_deg = t3[1];
                b->roll_deg = t3[2];
            }
        }
        BOOL("clear.enabled", p->clear.enabled);
        BOOL("clear.keep_inside", p->clear.keep_inside);
        else if (strcmp(k, "clear.min") == 0)
            parse_triple(v, p->clear.min);
        else if (strcmp(k, "clear.max") == 0)
            parse_triple(v, p->clear.max);
#undef NUM
#undef INT
#undef BOOL
    }

    pg_project_sanitise(p);
    return true;
}

bool pg_project_save(const PgProject *p, const char *path, char *err, size_t errcap)
{
    char buf[16384];
    size_t n = pg_project_write(p, buf, sizeof buf);
    if (n >= sizeof buf) {
        snprintf(err, errcap, "Project too large to write.");
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        plat_write_error(err, errcap, path, errno);
        return false;
    }
    bool ok = fwrite(buf, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok)
        snprintf(err, errcap, "Writing %s failed (disk full?).", path);
    return ok;
}

bool pg_project_load(PgProject *p, const char *path, char *err, size_t errcap)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errcap, "Cannot open %s.", path);
        return false;
    }
    char buf[32768];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    err[0] = '\0';
    return pg_project_parse(p, buf, err, errcap);
}
