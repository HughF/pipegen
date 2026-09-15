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
 * test_view3d.c — the rasteriser, the camera, and picking.
 *
 * Picking is how the operator selects what to bend, so it is tested the
 * way it is used: project a known point, read the id under it.
 */
#include "pg_test.h"
#include "pg_view3d.h"
#include "pg_model.h"
#include "pg_vec.h"

#include <stdlib.h>

static void test_raster_depth(void)
{
    PgRaster *r = pg_raster_new(64, 64);
    const uint8_t black[3] = { 0, 0, 0 };
    pg_raster_clear(r, black, black);
    CHECK(pg_raster_id_at(r, 10, 10) == -1);

    /* a far red triangle, then a near green one over half of it */
    PgRVert far[3] = { { 0, 0, 0.1f, 255, 0, 0 }, { 64, 0, 0.1f, 255, 0, 0 },
                       { 0, 64, 0.1f, 255, 0, 0 } };
    PgRVert near[3] = { { 0, 0, 0.5f, 0, 255, 0 }, { 32, 0, 0.5f, 0, 255, 0 },
                        { 0, 32, 0.5f, 0, 255, 0 } };
    pg_raster_tri(r, &near[0], &near[1], &near[2], 2, 1.0f, 0.0f);
    pg_raster_tri(r, &far[0], &far[1], &far[2], 1, 1.0f, 0.0f);

    CHECK(pg_raster_id_at(r, 5, 5) == 2);        /* near wins, drawn first */
    CHECK(r->rgba[(5 * 64 + 5) * 4 + 1] == 255);
    CHECK(pg_raster_id_at(r, 40, 10) == 1);
    CHECK(pg_raster_id_at(r, 60, 60) == -1);     /* outside both */

    /* translucent: blends, does not take the pixel */
    PgRVert glass[3] = { { 0, 0, 0.9f, 0, 0, 255 }, { 64, 0, 0.9f, 0, 0, 255 },
                         { 0, 64, 0.9f, 0, 0, 255 } };
    pg_raster_tri(r, &glass[0], &glass[1], &glass[2], 9, 0.5f, 0.0f);
    CHECK(pg_raster_id_at(r, 5, 5) == 2);
    CHECK(r->rgba[(5 * 64 + 5) * 4 + 2] > 100);

    uint8_t small[32 * 32 * 4];
    pg_raster_downsample(r, 2, small, 32, 32);
    CHECK(small[(2 * 32 + 2) * 4 + 3] == 255);
    pg_raster_free(r);
}

static void test_pick_chamber(void)
{
    PgModel *m = pg_model_new();
    const int W = 480, H = 300;
    PgRaster *r = pg_raster_new(W, H);
    PgCamera cam;
    pg_camera_default(&cam);
    pg_camera_preset(&cam, PG_VIEW_SIDE);
    pg_camera_fit(&cam, m->chain, true, (double)W / H);

    PgViewOpts o = { .colour = PG_COLOUR_SECTION, .show_engine = true,
                     .show_box = true, .show_seams = true, .show_grid = true,
                     .hover_id = -1, .select_id = -1, .highlight_section = -1 };
    m->project.clear.enabled = true;
    pg_model_update(m);
    pg_view3d_render(r, &cam, &m->project, m->chain, &o, &PG_VIEW_DARK);

    /* the middle of the belly, projected, is a belly piece */
    int belly = -1;
    for (int i = 0; i < m->chain->n_pieces; i++)
        if (m->chain->piece[i].sec_kind == PG_SEC_BELLY) {
            belly = i;
            break;
        }
    CHECK(belly >= 0);
    const PgPiece *pc = &m->chain->piece[belly];
    double mid[3], sx, sy;
    v3_add_scaled(mid, pc->p0, pc->axis, pc->len / 2.0);
    CHECK(pg_camera_project(&cam, W, H, mid, &sx, &sy));
    CHECK(sx > 0 && sx < W && sy > 0 && sy < H);
    int id = pg_raster_id_at(r, (int)sx, (int)sy);
    CHECK(PG_PICK_IS_PIECE(id));
    CHECK(PG_PICK_IS_PIECE(id) && m->chain->piece[id].sec_kind == PG_SEC_BELLY);

    /* a joint ring, on the silhouette edge of the pipe, picks the joint */
    const PgJoint *jt = &m->chain->joint[belly];      /* belly (1) -> (2) */
    double t = m->project.build.thickness_mm;
    double top[3];
    v3_add_scaled(top, jt->pos, (double[3]){ 0, 1, 0 }, pc->d1 / 2.0 + t);
    CHECK(pg_camera_project(&cam, W, H, top, &sx, &sy));
    int found = -1;
    for (int dy = -2; dy <= 2 && found < 0; dy++)
        for (int dx = -2; dx <= 2 && found < 0; dx++) {
            int k = pg_raster_id_at(r, (int)sx + dx, (int)sy + dy);
            if (PG_PICK_IS_JOINT(k))
                found = k - PG_PICK_JOINT;
        }
    CHECK(found == belly);

    /* the engine is there */
    double barrel[3] = { 0, 20, 0 };
    CHECK(pg_camera_project(&cam, W, H, barrel, &sx, &sy));
    CHECK(pg_raster_id_at(r, (int)sx, (int)sy) == PG_PICK_ENGINE);

    /* zooming about a point keeps that point under the cursor */
    double before_x, before_y, after_x, after_y;
    pg_camera_project(&cam, W, H, mid, &before_x, &before_y);
    pg_camera_zoom_at(&cam, 0.5, before_x, before_y, W, H);
    pg_camera_project(&cam, W, H, mid, &after_x, &after_y);
    CHECK_NEAR(after_x, before_x, 0.5);
    CHECK_NEAR(after_y, before_y, 0.5);

    /* every view renders a bent, hydroformed chamber without falling over */
    m->project.build.method = PG_MFG_HYDRO;
    pg_route_set_bend(&m->project.route, m->chain->joint[4].x, 1.0, 40.0, 90.0);
    pg_model_update(m);
    for (int v = PG_VIEW_3Q; v <= PG_VIEW_END; v++) {
        pg_camera_preset(&cam, (PgViewPreset)v);
        pg_camera_fit(&cam, m->chain, true, (double)W / H);
        pg_view3d_render(r, &cam, &m->project, m->chain, &o, &PG_VIEW_LIGHT);
        int hits = 0;
        for (int i = 0; i < W * H; i++)
            if (PG_PICK_IS_PIECE(r->id[i]))
                hits++;
        CHECK(hits > 200);
    }

    /* and from inside the pipe, which exercises near-plane clipping */
    v3_copy(cam.target, mid);
    cam.dist = 30.0;
    pg_view3d_render(r, &cam, &m->project, m->chain, &o, &PG_VIEW_DARK);

    pg_raster_free(r);
    pg_model_free(m);
}

/* The mesh is what the GPU draws, so check it stands on its own: indices in
 * range, every piece and joint present with its id, glass only for the box,
 * and rebuilding into the same mesh gives the same scene. */
static void test_mesh(void)
{
    PgModel *m = pg_model_new();
    m->project.clear.enabled = true;
    pg_model_update(m);
    PgViewOpts o = { .colour = PG_COLOUR_SECTION, .show_engine = true,
                     .show_box = true, .show_seams = true, .show_grid = true,
                     .hover_id = -1, .select_id = -1, .highlight_section = -1 };
    PgMesh mesh = { 0 };
    CHECK(pg_view3d_mesh(&mesh, &m->project, m->chain, &o, &PG_VIEW_DARK));
    CHECK(mesh.n_tri > 0 && mesh.n_tri % 3 == 0);
    CHECK(mesh.n_glass == 36);                   /* the box: 6 faces x 2 tris */

    int bad = 0;
    for (int i = 0; i < mesh.n_tri; i++)
        if (mesh.tri[i] >= (uint32_t)mesh.n_vert || mesh.vert[mesh.tri[i]].col[3] != 255)
            bad++;
    for (int i = 0; i < mesh.n_glass; i++)
        if (mesh.glass[i] >= (uint32_t)mesh.n_vert || mesh.vert[mesh.glass[i]].col[3] == 255)
            bad++;
    CHECK(bad == 0);

    int pieces = 0, engine = 0;
    bool *seen = calloc((size_t)m->chain->n_pieces, 1);
    for (int i = 0; i < mesh.n_vert; i++) {
        int id = mesh.vert[i].id;
        if (PG_PICK_IS_PIECE(id) && id < m->chain->n_pieces && !seen[id]) {
            seen[id] = true;
            pieces++;
        }
        if (id == PG_PICK_ENGINE)
            engine++;
    }
    CHECK(pieces == m->chain->n_pieces);
    CHECK(engine > 0);
    free(seen);

    int rings = 0;
    for (int j = 0; j < m->chain->n_joints; j++)
        for (int i = 0; i < mesh.n_line; i++)
            if (mesh.line[i].id == PG_PICK_JOINT + j) {
                rings++;
                break;
            }
    CHECK(rings == m->chain->n_joints);

    int n_vert = mesh.n_vert, n_line = mesh.n_line;
    o.show_box = false;
    CHECK(pg_view3d_mesh(&mesh, &m->project, m->chain, &o, &PG_VIEW_DARK));
    CHECK(mesh.n_glass == 0);
    o.show_box = true;
    CHECK(pg_view3d_mesh(&mesh, &m->project, m->chain, &o, &PG_VIEW_DARK));
    CHECK(mesh.n_vert == n_vert && mesh.n_line == n_line);

    pg_mesh_free(&mesh);
    CHECK(mesh.vert == NULL && mesh.n_vert == 0);
    pg_model_free(m);
}

/* The ring's hit area: a pointer a few pixels off the ring still finds it,
 * one well clear of every ring does not. */
static void test_pick_joint_near(void)
{
    PgModel *m = pg_model_new();
    const int W = 480, H = 300;
    PgCamera cam;
    pg_camera_default(&cam);
    pg_camera_preset(&cam, PG_VIEW_SIDE);
    pg_camera_fit(&cam, m->chain, true, (double)W / H);

    int j = m->chain->n_joints / 2;
    const PgJoint *jt = &m->chain->joint[j];
    const PgPiece *pc = &m->chain->piece[jt->before];
    double top[3], sx, sy;
    v3_add_scaled(top, jt->pos, (double[3]){ 0, 1, 0 },
                  pc->d1 / 2.0 + m->project.build.thickness_mm + 0.8);
    CHECK(pg_camera_project(&cam, W, H, top, &sx, &sy));

    CHECK(pg_view3d_pick_joint(&cam, W, H, &m->project, m->chain, sx, sy, 10.0)
          == PG_PICK_JOINT + j);
    CHECK(pg_view3d_pick_joint(&cam, W, H, &m->project, m->chain, sx + 6.0, sy, 10.0)
          == PG_PICK_JOINT + j);
    CHECK(pg_view3d_pick_joint(&cam, W, H, &m->project, m->chain, sx, sy - 6.0, 10.0)
          == PG_PICK_JOINT + j);
    CHECK(pg_view3d_pick_joint(&cam, W, H, &m->project, m->chain, sx, sy - 60.0, 10.0)
          == -1);
    CHECK(pg_view3d_pick_joint(&cam, W, H, &m->project, m->chain, sx + 6.0, sy, 3.0)
          == -1);
    pg_model_free(m);
}

TEST_MAIN("test_view3d",
    test_raster_depth();
    test_pick_chamber();
    test_mesh();
    test_pick_joint_near();
)
