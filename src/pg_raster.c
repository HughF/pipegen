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
#include "pg_raster.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

PgRaster *pg_raster_new(int w, int h)
{
    PgRaster *r = calloc(1, sizeof *r);
    if (!r)
        return NULL;
    if (!pg_raster_resize(r, w, h)) {
        free(r);
        return NULL;
    }
    return r;
}

bool pg_raster_resize(PgRaster *r, int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (r->rgba && r->w == w && r->h == h)
        return true;

    size_t n = (size_t)w * (size_t)h;
    uint8_t *rgba = malloc(n * 4);
    float   *depth = malloc(n * sizeof *depth);
    int32_t *id = malloc(n * sizeof *id);
    if (!rgba || !depth || !id) {
        free(rgba);
        free(depth);
        free(id);
        return false;
    }
    free(r->rgba);
    free(r->depth);
    free(r->id);
    r->rgba = rgba;
    r->depth = depth;
    r->id = id;
    r->w = w;
    r->h = h;
    return true;
}

void pg_raster_free(PgRaster *r)
{
    if (!r)
        return;
    free(r->rgba);
    free(r->depth);
    free(r->id);
    free(r);
}

void pg_raster_clear(PgRaster *r, const uint8_t top[3], const uint8_t bottom[3])
{
    for (int y = 0; y < r->h; y++) {
        float u = r->h > 1 ? (float)y / (float)(r->h - 1) : 0.0f;
        uint8_t c[4] = {
            (uint8_t)(top[0] + (bottom[0] - top[0]) * u),
            (uint8_t)(top[1] + (bottom[1] - top[1]) * u),
            (uint8_t)(top[2] + (bottom[2] - top[2]) * u),
            255
        };
        uint8_t *row = r->rgba + (size_t)y * (size_t)r->w * 4;
        for (int x = 0; x < r->w; x++)
            memcpy(row + x * 4, c, 4);
    }
    size_t n = (size_t)r->w * (size_t)r->h;
    memset(r->depth, 0, n * sizeof *r->depth);
    for (size_t i = 0; i < n; i++)
        r->id[i] = -1;
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

void pg_raster_tri(PgRaster *r, const PgRVert *a, const PgRVert *b,
                   const PgRVert *c, int32_t id, float alpha, float bias)
{
    float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (fabsf(area) < 1e-8f)
        return;

    float minx = fminf(a->x, fminf(b->x, c->x));
    float maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y));
    float maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > r->w - 1) x1 = r->w - 1;
    if (y1 > r->h - 1) y1 = r->h - 1;
    if (x0 > x1 || y0 > y1)
        return;

    bool opaque = alpha >= 1.0f;
    float inv = 1.0f / area;
    float k = 1.0f + bias;

    /* perspective-correct colour: interpolate c/z and 1/z */
    float ar = a->r * a->iz, ag = a->g * a->iz, ab = a->b * a->iz;
    float br = b->r * b->iz, bg = b->g * b->iz, bb = b->b * b->iz;
    float cr = c->r * c->iz, cg = c->g * c->iz, cb = c->b * c->iz;

    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        float *drow = r->depth + (size_t)y * (size_t)r->w;
        for (int x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f;
            float w0 = edge(b->x, b->y, c->x, c->y, px, py) * inv;
            float w1 = edge(c->x, c->y, a->x, a->y, px, py) * inv;
            float w2 = 1.0f - w0 - w1;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                continue;

            float iz = w0 * a->iz + w1 * b->iz + w2 * c->iz;
            float z = iz * k;
            if (z <= drow[x])
                continue;

            float R = (w0 * ar + w1 * br + w2 * cr) / iz;
            float G = (w0 * ag + w1 * bg + w2 * cg) / iz;
            float B = (w0 * ab + w1 * bb + w2 * cb) / iz;
            uint8_t *px4 = r->rgba + ((size_t)y * (size_t)r->w + (size_t)x) * 4;

            if (opaque) {
                px4[0] = (uint8_t)fminf(255.0f, fmaxf(0.0f, R));
                px4[1] = (uint8_t)fminf(255.0f, fmaxf(0.0f, G));
                px4[2] = (uint8_t)fminf(255.0f, fmaxf(0.0f, B));
                drow[x] = z;
                r->id[(size_t)y * (size_t)r->w + (size_t)x] = id;
            } else {
                px4[0] = (uint8_t)(px4[0] + (fminf(255.0f, R) - px4[0]) * alpha);
                px4[1] = (uint8_t)(px4[1] + (fminf(255.0f, G) - px4[1]) * alpha);
                px4[2] = (uint8_t)(px4[2] + (fminf(255.0f, B) - px4[2]) * alpha);
            }
        }
    }
}

void pg_raster_line(PgRaster *r, const PgRVert *a, const PgRVert *b,
                    float width_px, int32_t id, float alpha, float bias)
{
    float dx = b->x - a->x, dy = b->y - a->y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) {
        dx = 1.0f;
        dy = 0.0f;
        len = 1.0f;
    }
    float nx = -dy / len * width_px * 0.5f, ny = dx / len * width_px * 0.5f;
    /* stretch past the ends a little so joined segments leave no gaps */
    float ex = dx / len * width_px * 0.35f, ey = dy / len * width_px * 0.35f;

    PgRVert q[4] = { *a, *a, *b, *b };
    q[0].x = a->x + nx - ex; q[0].y = a->y + ny - ey;
    q[1].x = a->x - nx - ex; q[1].y = a->y - ny - ey;
    q[2].x = b->x - nx + ex; q[2].y = b->y - ny + ey;
    q[3].x = b->x + nx + ex; q[3].y = b->y + ny + ey;
    pg_raster_tri(r, &q[0], &q[1], &q[2], id, alpha, bias);
    pg_raster_tri(r, &q[0], &q[2], &q[3], id, alpha, bias);
}

int32_t pg_raster_id_at(const PgRaster *r, int x, int y)
{
    if (!r || x < 0 || y < 0 || x >= r->w || y >= r->h)
        return -1;
    return r->id[(size_t)y * (size_t)r->w + (size_t)x];
}

void pg_raster_downsample(const PgRaster *r, int factor, uint8_t *dst,
                          int dw, int dh)
{
    if (factor <= 1) {
        for (int y = 0; y < dh && y < r->h; y++)
            memcpy(dst + (size_t)y * (size_t)dw * 4,
                   r->rgba + (size_t)y * (size_t)r->w * 4,
                   (size_t)(dw < r->w ? dw : r->w) * 4);
        return;
    }
    int n = factor * factor;
    for (int y = 0; y < dh; y++) {
        for (int x = 0; x < dw; x++) {
            int sum[4] = { 0, 0, 0, 0 };
            for (int j = 0; j < factor; j++) {
                int sy = y * factor + j;
                if (sy >= r->h) sy = r->h - 1;
                const uint8_t *row = r->rgba + (size_t)sy * (size_t)r->w * 4;
                for (int i = 0; i < factor; i++) {
                    int sx = x * factor + i;
                    if (sx >= r->w) sx = r->w - 1;
                    for (int k = 0; k < 4; k++)
                        sum[k] += row[sx * 4 + k];
                }
            }
            uint8_t *o = dst + ((size_t)y * (size_t)dw + (size_t)x) * 4;
            for (int k = 0; k < 4; k++)
                o[k] = (uint8_t)(sum[k] / n);
        }
    }
}
