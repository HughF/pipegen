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
    uint8_t sharp[3];      /* a joint that breaks the bend rules */
} PgViewStyle;

extern const PgViewStyle PG_VIEW_DARK;
extern const PgViewStyle PG_VIEW_LIGHT;

/*
 * The scene as geometry: independent of the camera, so it is built once per
 * change and drawn from any angle — by the software rasteriser here, or by
 * the GPU (pg_glview). Opaque and translucent triangles index one vertex
 * array; lines are kept as segments because both renderers widen them in
 * screen space.
 *
 * Line widths are in pixels of the supersampled raster the view is normally
 * drawn at (twice the screen's pixels on an ordinary display).
 */
typedef struct {
    float   p[3];
    float   n[3];          /* zero for a surface drawn unlit          */
    uint8_t col[4];        /* alpha < 255 only in the translucent list */
    int32_t id;            /* a PG_PICK value                          */
    float   bias;          /* towards the viewer, fraction of depth    */
} PgMeshVert;

typedef struct {
    float   a[3], b[3];
    uint8_t col[4];
    int32_t id;
    float   width;
    float   bias;
} PgMeshLine;

typedef struct {
    PgMeshVert *vert;  int n_vert, cap_vert;
    uint32_t   *tri;   int n_tri, cap_tri;       /* opaque: 3 indices each      */
    uint32_t   *glass; int n_glass, cap_glass;   /* translucent: 3 indices each */
    PgMeshLine *line;  int n_line, cap_line;
    bool        oom;
} PgMesh;

/* The camera as a frame: view space is (right, up, forward) from the eye,
 * and the screen point of view-space v is (cx + focal v.x / v.z,
 * cy - focal v.y / v.z). light is the key light's direction. */
typedef struct {
    double eye[3], r[3], u[3], f[3];
    double light[3];
    double focal, cx, cy;
    double near_z;
} PgViewFrame;

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

void pg_camera_frame(const PgCamera *cam, int view_w, int view_h, PgViewFrame *fr);

/* Build the scene into m, reusing its allocations. False if out of memory
 * (m then holds part of the scene). */
bool pg_view3d_mesh(PgMesh *m, const PgProject *pr, const PgChain *c,
                    const PgViewOpts *o, const PgViewStyle *st);
void pg_mesh_free(PgMesh *m);

/* Draw a built scene into the raster with the software rasteriser. */
void pg_view3d_draw(PgRaster *r, const PgCamera *cam, const PgMesh *m,
                    const PgViewStyle *st);

/* Build and draw in one go. */
void pg_view3d_render(PgRaster *r, const PgCamera *cam, const PgProject *pr,
                      const PgChain *c, const PgViewOpts *o,
                      const PgViewStyle *st);

#endif /* PG_VIEW3D_H */
