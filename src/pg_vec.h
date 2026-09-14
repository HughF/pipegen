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
/* pg_vec.h — three-dimensional vectors, as double[3]. */
#ifndef PG_VEC_H
#define PG_VEC_H

#include <math.h>

static inline void v3_set(double *o, double x, double y, double z)
{
    o[0] = x; o[1] = y; o[2] = z;
}

static inline void v3_copy(double *o, const double *a)
{
    o[0] = a[0]; o[1] = a[1]; o[2] = a[2];
}

static inline double v3_dot(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void v3_cross(double *o, const double *a, const double *b)
{
    double x = a[1] * b[2] - a[2] * b[1];
    double y = a[2] * b[0] - a[0] * b[2];
    double z = a[0] * b[1] - a[1] * b[0];
    o[0] = x; o[1] = y; o[2] = z;
}

static inline double v3_len(const double *a)
{
    return sqrt(v3_dot(a, a));
}

static inline void v3_norm(double *a)
{
    double l = v3_len(a);
    if (l > 0.0) {
        a[0] /= l; a[1] /= l; a[2] /= l;
    }
}

/* o = a + s * b (o may alias a or b) */
static inline void v3_add_scaled(double *o, const double *a, const double *b,
                                 double s)
{
    o[0] = a[0] + s * b[0];
    o[1] = a[1] + s * b[1];
    o[2] = a[2] + s * b[2];
}

static inline void v3_sub(double *o, const double *a, const double *b)
{
    o[0] = a[0] - b[0];
    o[1] = a[1] - b[1];
    o[2] = a[2] - b[2];
}

/* Rotate v about the unit axis k by angle t (Rodrigues). */
static inline void v3_rotate(double *v, const double *k, double t)
{
    double c = cos(t), s = sin(t);
    double kxv[3];
    v3_cross(kxv, k, v);
    double kd = v3_dot(k, v) * (1.0 - c);
    for (int i = 0; i < 3; i++)
        v[i] = v[i] * c + kxv[i] * s + k[i] * kd;
}

#endif /* PG_VEC_H */
