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
#include "pg_geom.h"

#include <math.h>

#define BULGE_EPS 1e-12

double pg_bulge_for_sweep(double sweep_rad)
{
    return tan(sweep_rad / 4.0);
}

/*
 * With chord p0->p1 of length L, the centre lies on the chord's left normal
 * at (L/2)(1 - b^2)/(2b) from its midpoint — left for a counter-clockwise
 * arc, and the formula's sign flips it right for a clockwise one.
 */
void pg_bulge_arc(double x0, double y0, double x1, double y1, double bulge,
                  double *cx, double *cy, double *r, double *a0, double *sweep)
{
    double ux = x1 - x0, uy = y1 - y0;
    double L = sqrt(ux * ux + uy * uy);
    double th = 4.0 * atan(bulge);

    if (L <= 0.0 || fabs(bulge) < BULGE_EPS) {
        *cx = x0; *cy = y0; *r = 0.0; *a0 = 0.0; *sweep = 0.0;
        return;
    }

    double nx = -uy / L, ny = ux / L;
    double off = (L / 2.0) * (1.0 - bulge * bulge) / (2.0 * bulge);
    *cx = (x0 + x1) / 2.0 + nx * off;
    *cy = (y0 + y1) / 2.0 + ny * off;
    *r = fabs(L / (2.0 * sin(th / 2.0)));
    *a0 = atan2(y0 - *cy, x0 - *cx);
    *sweep = th;
}

void pg_xform(double x, double y, double rot, double dx, double dy,
              double *ox, double *oy)
{
    double c = cos(rot), s = sin(rot);
    *ox = x * c - y * s + dx;
    *oy = x * s + y * c + dy;
}

void pg_path_flatten(const PgVert *v, int n, bool closed, double max_step_rad,
                     PgPointFn fn, void *user)
{
    if (n <= 0)
        return;
    if (max_step_rad <= 0.0)
        max_step_rad = 0.05;

    int segs = closed ? n : n - 1;
    fn(v[0].x, v[0].y, user);

    for (int i = 0; i < segs; i++) {
        const PgVert *p = &v[i], *q = &v[(i + 1) % n];

        if (fabs(p->bulge) >= BULGE_EPS) {
            double cx, cy, r, a0, sw;
            pg_bulge_arc(p->x, p->y, q->x, q->y, p->bulge, &cx, &cy, &r, &a0, &sw);
            int steps = (int)ceil(fabs(sw) / max_step_rad);
            if (steps < 1)
                steps = 1;
            for (int k = 1; k < steps; k++) {
                double a = a0 + sw * (double)k / steps;
                fn(cx + r * cos(a), cy + r * sin(a), user);
            }
        }
        fn(q->x, q->y, user);
    }
}

static void box_add(PgBox *b, double x, double y)
{
    if (x < b->minx) b->minx = x;
    if (y < b->miny) b->miny = y;
    if (x > b->maxx) b->maxx = x;
    if (y > b->maxy) b->maxy = y;
}

/* True if angle `a` lies on the arc from a0 sweeping sw. */
static bool angle_on_arc(double a, double a0, double sw)
{
    double rel = (sw >= 0.0) ? a - a0 : a0 - a;
    rel = fmod(rel, 2.0 * M_PI);
    if (rel < 0.0)
        rel += 2.0 * M_PI;
    return rel <= fabs(sw);
}

PgBox pg_path_box(const PgVert *v, int n, bool closed)
{
    PgBox b = { HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL };
    if (n <= 0) {
        PgBox z = { 0, 0, 0, 0 };
        return z;
    }

    int segs = closed ? n : n - 1;
    for (int i = 0; i < n; i++)
        box_add(&b, v[i].x, v[i].y);

    for (int i = 0; i < segs; i++) {
        const PgVert *p = &v[i], *q = &v[(i + 1) % n];
        if (fabs(p->bulge) < BULGE_EPS)
            continue;
        double cx, cy, r, a0, sw;
        pg_bulge_arc(p->x, p->y, q->x, q->y, p->bulge, &cx, &cy, &r, &a0, &sw);
        for (int k = 0; k < 4; k++) {
            double a = k * M_PI / 2.0;
            if (angle_on_arc(a, a0, sw))
                box_add(&b, cx + r * cos(a), cy + r * sin(a));
        }
    }
    return b;
}

double pg_path_area(const PgVert *v, int n)
{
    double area = 0.0;
    for (int i = 0; i < n; i++) {
        const PgVert *p = &v[i], *q = &v[(i + 1) % n];
        area += (p->x * q->y - q->x * p->y) / 2.0;
        if (fabs(p->bulge) >= BULGE_EPS) {
            double cx, cy, r, a0, sw;
            pg_bulge_arc(p->x, p->y, q->x, q->y, p->bulge, &cx, &cy, &r, &a0, &sw);
            /* circular segment between chord and arc; sin is odd, so the
             * sign of the sweep carries through */
            area += r * r / 2.0 * (sw - sin(sw));
        }
    }
    return area;
}

double pg_path_length(const PgVert *v, int n, bool closed)
{
    double len = 0.0;
    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; i++) {
        const PgVert *p = &v[i], *q = &v[(i + 1) % n];
        if (fabs(p->bulge) >= BULGE_EPS) {
            double cx, cy, r, a0, sw;
            pg_bulge_arc(p->x, p->y, q->x, q->y, p->bulge, &cx, &cy, &r, &a0, &sw);
            len += r * fabs(sw);
        } else {
            len += hypot(q->x - p->x, q->y - p->y);
        }
    }
    return len;
}
