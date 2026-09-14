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
 * pg_geom.h — outlines as bulge polylines
 *
 * A flat pattern is a closed polyline whose segments are straight lines or
 * circular arcs. An arc is stored the way DXF stores it: a "bulge" on the
 * vertex it starts from, the tangent of a quarter of its included angle,
 * positive for counter-clockwise. That keeps arcs exact all the way to the
 * laser — the DXF gets true arcs, the PDF gets Béziers fitted to them, and
 * only the screen preview is ever flattened into short lines.
 *
 * A bulge is unchanged by rotation and translation, so placing a part on a
 * sheet never touches its arcs.
 */
#ifndef PG_GEOM_H
#define PG_GEOM_H

#include <stdbool.h>

typedef struct {
    double x, y;
    double bulge;          /* arc to the next vertex; 0 for a line */
} PgVert;

typedef struct {
    double minx, miny, maxx, maxy;
} PgBox;

/* Bulge for an arc sweeping `sweep_rad` (signed, CCW positive). */
double pg_bulge_for_sweep(double sweep_rad);

/* Recover the circle an arc lies on. sweep is signed, radians. */
void pg_bulge_arc(double x0, double y0, double x1, double y1, double bulge,
                  double *cx, double *cy, double *r, double *a0, double *sweep);

/* Rotate by `rot` radians about the origin, then translate. */
void pg_xform(double x, double y, double rot, double dx, double dy,
              double *ox, double *oy);

typedef void (*PgPointFn)(double x, double y, void *user);

/* Walk the outline as points, arcs subdivided so no step exceeds
 * `max_step_rad`. Every vertex is emitted; a closed outline also emits the
 * first point again at the end. */
void pg_path_flatten(const PgVert *v, int n, bool closed, double max_step_rad,
                     PgPointFn fn, void *user);

/* Exact bounding box: arcs contribute their extreme points, not just their
 * ends. */
PgBox pg_path_box(const PgVert *v, int n, bool closed);

/* Signed area of a closed outline (CCW positive), arcs included exactly. */
double pg_path_area(const PgVert *v, int n);

/* Perimeter length, arcs included exactly. */
double pg_path_length(const PgVert *v, int n, bool closed);

#endif /* PG_GEOM_H */
