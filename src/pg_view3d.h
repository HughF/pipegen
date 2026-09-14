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
 * pg_view3d.h — the chamber, the engine and the clearance box in 3D
 *
 * An orbiting camera about a target point, and a renderer that draws the
 * scene into a PgRaster: shaded pieces coloured by section or by part, the
 * weld joints as rings that can be picked and bent, seams, hydroforming
 * flanges, a rough engine for scale, a ground grid and the clearance box.
 *
 * Engine coordinates: +Y up, the crankshaft along Z, the port facing +X.
 */
#ifndef PG_VIEW3D_H
#define PG_VIEW3D_H

#include <stdbool.h>
#include <stdint.h>

#include "pg_raster.h"
#include "pg_project.h"
#include "pg_route.h"
#include "pg_pattern.h"

/* What a pixel belongs to, from pg_raster_id_at. */
#define PG_PICK_NONE    (-1)
#define PG_PICK_JOINT   1000     /* + joint index */
#define PG_PICK_ENGINE  2000
#define PG_PICK_IS_PIECE(id) ((id) >= 0 && (id) < PG_PICK_JOINT)
#define PG_PICK_IS_JOINT(id) ((id) >= PG_PICK_JOINT && (id) < PG_PICK_ENGINE)

typedef struct {
    double yaw, pitch;     /* radians; yaw 0 looks along -Z (side view) */
    double dist;           /* mm from target                           */
    double target[3];
    double fov;            /* vertical, radians                        */
} PgCamera;

typedef enum {
    PG_VIEW_3Q = 0,        /* three-quarter */
    PG_VIEW_SIDE,
    PG_VIEW_TOP,
    PG_VIEW_END
} PgViewPreset;

typedef enum {
    PG_COLOUR_SECTION = 0,
    PG_COLOUR_PART,
    PG_COLOUR_METAL
} PgColourMode;

typedef struct {
    PgColourMode colour;
    bool show_engine;
    bool show_box;
    bool show_seams;
    bool show_grid;
    int  hover_id;         /* a PG_PICK value, or -1                   */
    int  select_id;
    int  highlight_section;
} PgViewOpts;

typedef struct {
    uint8_t bg_top[3], bg_bottom[3];
    uint8_t grid[3], grid_major[3];
    uint8_t select[3], hover[3], clash[3], bend[3];
    uint8_t joint[3], seam[3], box[3], engine[3];
} PgViewStyle;

extern const PgViewStyle PG_VIEW_DARK;
extern const PgViewStyle PG_VIEW_LIGHT;

void pg_camera_default(PgCamera *cam);
void pg_camera_preset(PgCamera *cam, PgViewPreset v);

/* Frame the chamber (and the engine, if shown) in a view of this aspect. */
void pg_camera_fit(PgCamera *cam, const PgChain *c, bool with_engine,
                   double aspect);

void pg_camera_orbit(PgCamera *cam, double dx_px, double dy_px);
void pg_camera_pan(PgCamera *cam, double dx_px, double dy_px, int view_h);

/* Zoom by `factor` (<1 closer) about the screen point (sx, sy). */
void pg_camera_zoom_at(PgCamera *cam, double factor, double sx, double sy,
                       int view_w, int view_h);

/* Project an engine-coordinate point to the screen. False if behind the
 * viewer. */
bool pg_camera_project(const PgCamera *cam, int view_w, int view_h,
                       const double p[3], double *sx, double *sy);

void pg_view3d_render(PgRaster *r, const PgCamera *cam, const PgProject *pr,
                      const PgChain *c, const PgViewOpts *o,
                      const PgViewStyle *st);

#endif /* PG_VIEW3D_H */
