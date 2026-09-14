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
 * pg_raster.h — a small software rasteriser
 *
 * Triangles and thick lines into a colour buffer with a depth buffer and an
 * id buffer. The depth buffer is what makes a bent chamber, the engine and a
 * clearance box occlude each other correctly from every angle, which sorting
 * polygons by distance does not. The id buffer records which piece, joint or
 * object owns each pixel, so picking under the pointer is exact.
 *
 * No SDL here: the buffer is uploaded to a texture by the UI, and rendered
 * headless for the PDF and the tests.
 */
#ifndef PG_RASTER_H
#define PG_RASTER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int      w, h;
    uint8_t *rgba;       /* w * h * 4                               */
    float   *depth;      /* 1/z; 0 is infinitely far                */
    int32_t *id;         /* -1 where nothing was drawn              */
} PgRaster;

/* A vertex in screen space: pixels, reciprocal view depth, colour 0..255. */
typedef struct {
    float x, y, iz;
    float r, g, b;
} PgRVert;

PgRaster *pg_raster_new(int w, int h);
bool      pg_raster_resize(PgRaster *r, int w, int h);
void      pg_raster_free(PgRaster *r);

/* Clear to a vertical gradient, top to bottom. */
void pg_raster_clear(PgRaster *r, const uint8_t top[3], const uint8_t bottom[3]);

/*
 * Opaque when alpha >= 1: depth-tested, writes depth and id. Translucent
 * otherwise: depth-tested, blended, writes neither. `bias` moves the shape
 * towards the viewer by that fraction of its depth, so a line on a surface
 * wins against the surface.
 */
void pg_raster_tri(PgRaster *r, const PgRVert *a, const PgRVert *b,
                   const PgRVert *c, int32_t id, float alpha, float bias);

void pg_raster_line(PgRaster *r, const PgRVert *a, const PgRVert *b,
                    float width_px, int32_t id, float alpha, float bias);

int32_t pg_raster_id_at(const PgRaster *r, int x, int y);

/* Box-filter down by `factor` into dst (dw * dh * 4, RGBA). */
void pg_raster_downsample(const PgRaster *r, int factor, uint8_t *dst,
                          int dw, int dh);

#endif /* PG_RASTER_H */
