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
#include "pg_dxf.h"
#include "pg_version.h"
#include "plat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* Gap between sheets when several are laid side by side in one file. */
#define SHEET_SPACING 200.0

typedef struct {
    FILE *f;
    double minx, miny, maxx, maxy;
} Dxf;

static void g_str(Dxf *d, int code, const char *s)
{
    fprintf(d->f, "%3d\n%s\n", code, s);
}

static void g_int(Dxf *d, int code, int v)
{
    fprintf(d->f, "%3d\n%6d\n", code, v);
}

static void g_num(Dxf *d, int code, double v)
{
    if (fabs(v) < 5e-7)
        v = 0.0;                           /* no "-0.000000" */
    fprintf(d->f, "%3d\n%.6f\n", code, v);
}

/* DXF R12 text is 7-bit here: anything else becomes '?', and the file stays
 * readable by the oldest importer. */
static void g_text(Dxf *d, int code, const char *s)
{
    char buf[256];
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)s;
         *p && n + 1 < sizeof buf; p++) {
        if (*p < 0x80) {
            buf[n++] = (char)*p;
        } else if ((*p & 0xC0) == 0xC0) {
            buf[n++] = '?';                /* lead byte of a multi-byte char */
        }
    }
    buf[n] = '\0';
    g_str(d, code, buf);
}

static void extent(Dxf *d, double x, double y)
{
    if (x < d->minx) d->minx = x;
    if (y < d->miny) d->miny = y;
    if (x > d->maxx) d->maxx = x;
    if (y > d->maxy) d->maxy = y;
}

static void header(Dxf *d, double minx, double miny, double maxx, double maxy)
{
    g_str(d, 0, "SECTION");
    g_str(d, 2, "HEADER");
    g_str(d, 9, "$ACADVER");  g_str(d, 1, "AC1009");
    g_str(d, 9, "$INSBASE");  g_num(d, 10, 0); g_num(d, 20, 0); g_num(d, 30, 0);
    g_str(d, 9, "$EXTMIN");   g_num(d, 10, minx); g_num(d, 20, miny); g_num(d, 30, 0);
    g_str(d, 9, "$EXTMAX");   g_num(d, 10, maxx); g_num(d, 20, maxy); g_num(d, 30, 0);
    /* Millimetres. Neither variable is R12, but readers that know them use
     * them and readers that do not skip them. */
    g_str(d, 9, "$MEASUREMENT"); g_int(d, 70, 1);
    g_str(d, 9, "$INSUNITS");    g_int(d, 70, 4);
    g_str(d, 0, "ENDSEC");

    g_str(d, 0, "SECTION");
    g_str(d, 2, "TABLES");

    g_str(d, 0, "TABLE");
    g_str(d, 2, "LTYPE");
    g_int(d, 70, 1);
    g_str(d, 0, "LTYPE");
    g_str(d, 2, "CONTINUOUS");
    g_int(d, 70, 0);
    g_str(d, 3, "Solid line");
    g_int(d, 72, 65);
    g_int(d, 73, 0);
    g_num(d, 40, 0.0);
    g_str(d, 0, "ENDTAB");

    static const struct { const char *name; int colour; } layers[] = {
        { "CUT", 7 }, { "ETCH", 5 }, { "LABEL", 3 }, { "SHEET", 8 },
    };
    g_str(d, 0, "TABLE");
    g_str(d, 2, "LAYER");
    g_int(d, 70, 4);
    for (int i = 0; i < 4; i++) {
        g_str(d, 0, "LAYER");
        g_str(d, 2, layers[i].name);
        g_int(d, 70, 0);
        g_int(d, 62, layers[i].colour);
        g_str(d, 6, "CONTINUOUS");
    }
    g_str(d, 0, "ENDTAB");
    g_str(d, 0, "ENDSEC");

    g_str(d, 0, "SECTION");
    g_str(d, 2, "BLOCKS");
    g_str(d, 0, "ENDSEC");

    g_str(d, 0, "SECTION");
    g_str(d, 2, "ENTITIES");
}

static void footer(Dxf *d)
{
    g_str(d, 0, "ENDSEC");
    g_str(d, 0, "EOF");
}

static void polyline(Dxf *d, const char *layer, const PgVert *v, int n,
                     double rot, double dx, double dy)
{
    g_str(d, 0, "POLYLINE");
    g_str(d, 8, layer);
    g_int(d, 66, 1);
    g_num(d, 10, 0); g_num(d, 20, 0); g_num(d, 30, 0);
    g_int(d, 70, 1);                           /* closed */
    for (int i = 0; i < n; i++) {
        double x, y;
        pg_xform(v[i].x, v[i].y, rot, dx, dy, &x, &y);
        g_str(d, 0, "VERTEX");
        g_str(d, 8, layer);
        g_num(d, 10, x); g_num(d, 20, y); g_num(d, 30, 0);
        if (fabs(v[i].bulge) > 1e-12)
            g_num(d, 42, v[i].bulge);
    }
    g_str(d, 0, "SEQEND");
    g_str(d, 8, layer);
}

static void line(Dxf *d, const char *layer, double x0, double y0,
                 double x1, double y1)
{
    g_str(d, 0, "LINE");
    g_str(d, 8, layer);
    g_num(d, 10, x0); g_num(d, 20, y0); g_num(d, 30, 0);
    g_num(d, 11, x1); g_num(d, 21, y1); g_num(d, 31, 0);
}

static void text(Dxf *d, const char *layer, double x, double y, double h,
                 double rot_deg, const char *s)
{
    g_str(d, 0, "TEXT");
    g_str(d, 8, layer);
    g_num(d, 10, x); g_num(d, 20, y); g_num(d, 30, 0);
    g_num(d, 40, h);
    g_text(d, 1, s);
    if (fabs(rot_deg) > 1e-9)
        g_num(d, 50, rot_deg);
    g_int(d, 72, 1);                           /* centred ...          */
    g_num(d, 11, x); g_num(d, 21, y); g_num(d, 31, 0);
    g_int(d, 73, 2);                           /* ... on its middle    */
}

/* Label height proportional to the part, within limits that stay legible and
 * inside the outline. */
static double label_height(const PgPart *p)
{
    double w = p->box.maxx - p->box.minx, h = p->box.maxy - p->box.miny;
    double m = fmin(w, h);
    if (p->kind == PG_PART_CONE)
        m = fmin(m, p->slant);
    double t = m / 8.0;
    return t < 3.0 ? 3.0 : t > 14.0 ? 14.0 : t;
}

static void emit_part(Dxf *d, const PgProject *pr, const PgPart *p,
                      double rot, double dx, double dy)
{
    polyline(d, "CUT", p->v, p->n, rot, dx, dy);

    if (pr->build.etch_marks) {
        for (int i = 0; i < p->n_marks; i++) {
            double x0, y0, x1, y1;
            pg_xform(p->marks[i].x0, p->marks[i].y0, rot, dx, dy, &x0, &y0);
            pg_xform(p->marks[i].x1, p->marks[i].y1, rot, dx, dy, &x1, &y1);
            line(d, "ETCH", x0, y0, x1, y1);
        }
    }

    /* Keep the label upright-ish: a rotation past a quarter turn would read
     * upside down. */
    double deg = fmod(rot * 180.0 / M_PI, 360.0);
    if (deg > 90.0 && deg <= 270.0) deg -= 180.0;
    if (deg > 270.0) deg -= 360.0;

    double lx, ly;
    pg_xform(p->label_x, p->label_y, rot, dx, dy, &lx, &ly);
    text(d, "LABEL", lx, ly, label_height(p), deg, p->id);
}

static bool finish_file(Dxf *d, const char *path, char *err, size_t errcap)
{
    footer(d);
    bool ok = !ferror(d->f);
    ok = (fclose(d->f) == 0) && ok;
    if (!ok)
        snprintf(err, errcap, "Writing %s failed.", path);
    return ok;
}

bool pg_dxf_write_layout(const char *path, const PgProject *pr,
                         const PgPartList *parts, const PgLayout *lay,
                         char *err, size_t errcap)
{
    if (lay->n == 0) {
        snprintf(err, errcap, "Nothing to cut: every part is from tube.");
        return false;
    }

    Dxf d = { NULL, HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL };

    /* Sheet origins: side by side along x. An oversize sheet is widened to
     * the part it holds, so the next sheet still starts clear of it. */
    double origin[PG_MAX_SHEETS];
    double sheet_w[PG_MAX_SHEETS], sheet_h[PG_MAX_SHEETS];
    for (int s = 0; s < lay->n_sheets; s++) {
        sheet_w[s] = lay->sheet_w;
        sheet_h[s] = lay->sheet_h;
    }
    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        const PgPart *p = &parts->part[pl->part];
        for (int k = 0; k < p->n; k++) {
            double x, y;
            pg_place_point(pl, p->v[k].x, p->v[k].y, &x, &y);
            if (x + lay->gap > sheet_w[pl->sheet]) sheet_w[pl->sheet] = x + lay->gap;
            if (y + lay->gap > sheet_h[pl->sheet]) sheet_h[pl->sheet] = y + lay->gap;
        }
    }
    double ox = 0.0;
    for (int s = 0; s < lay->n_sheets; s++) {
        origin[s] = ox;
        ox += sheet_w[s] + SHEET_SPACING;
        extent(&d, origin[s], 0.0);
        extent(&d, origin[s] + sheet_w[s], sheet_h[s] + 60.0);
    }

    d.f = fopen(path, "w");
    if (!d.f) {
        plat_write_error(err, errcap, path, errno);
        return false;
    }
    header(&d, d.minx, d.miny, d.maxx, d.maxy);

    for (int s = 0; s < lay->n_sheets; s++) {
        PgVert border[4] = {
            { origin[s], 0.0, 0 },                     { origin[s] + sheet_w[s], 0.0, 0 },
            { origin[s] + sheet_w[s], sheet_h[s], 0 }, { origin[s], sheet_h[s], 0 },
        };
        polyline(&d, "SHEET", border, 4, 0.0, 0.0, 0.0);

        char title[200];
        snprintf(title, sizeof title,
                 "%s - sheet %d of %d - %.0f x %.0f mm - %.1f mm %s%s",
                 pr->title, s + 1, lay->n_sheets, lay->sheet_w, lay->sheet_h,
                 pr->build.thickness_mm, pg_material_name(pr->build.material),
                 lay->oversize[s] ? " - PART LARGER THAN SHEET" : "");
        g_str(&d, 0, "TEXT");
        g_str(&d, 8, "SHEET");
        g_num(&d, 10, origin[s]); g_num(&d, 20, sheet_h[s] + 20.0); g_num(&d, 30, 0);
        g_num(&d, 40, 18.0);
        g_text(&d, 1, title);
    }

    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        emit_part(&d, pr, &parts->part[pl->part], pl->rot,
                  origin[pl->sheet] + pl->x, pl->y);
    }

    return finish_file(&d, path, err, errcap);
}

bool pg_dxf_write_part(const char *path, const PgProject *pr,
                       const PgPart *part, char *err, size_t errcap)
{
    if (part->kind == PG_PART_TUBE || part->n == 0) {
        snprintf(err, errcap, "%s is cut from tube; it has no flat pattern.",
                 part->id);
        return false;
    }

    Dxf d = { NULL, 0, 0, 0, 0 };
    double dx = -part->box.minx, dy = -part->box.miny;

    d.f = fopen(path, "w");
    if (!d.f) {
        plat_write_error(err, errcap, path, errno);
        return false;
    }
    header(&d, 0.0, 0.0, part->box.maxx - part->box.minx,
           part->box.maxy - part->box.miny);
    emit_part(&d, pr, part, 0.0, dx, dy);
    return finish_file(&d, path, err, errcap);
}
