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
 * test_pattern.c — the flat patterns must roll up into the chamber.
 *
 * These are the checks that matter on the bench: an arc that is 0.5 mm
 * short is a cone that will not close, and a mitre edge that is not the
 * length of the curve it wraps to is a joint that will not meet.
 */
#include "pg_test.h"
#include "pg_pattern.h"
#include "pg_vec.h"

#include <stdlib.h>

typedef struct {
    PgProject   p;
    PgDesign    d;
    PgChain    *c;
    PgPartList *pl;
} Build;

static Build *build_new(void)
{
    Build *b = calloc(1, sizeof *b);
    b->c = calloc(1, sizeof *b->c);
    b->pl = calloc(1, sizeof *b->pl);
    pg_project_default(&b->p);
    return b;
}

static void build_run(Build *b)
{
    pg_design_compute(&b->p, &b->d);
    pg_chain_build(&b->p, &b->d, b->c);
    pg_parts_build(&b->p, &b->d, b->c, b->pl);
}

static void build_free(Build *b)
{
    free(b->c);
    free(b->pl);
    free(b);
}

static void test_bulge_maths(void)
{
    double cx, cy, r, a0, sw;
    pg_bulge_arc(10, 0, 0, 10, pg_bulge_for_sweep(M_PI / 2), &cx, &cy, &r, &a0, &sw);
    CHECK_NEAR(cx, 0.0, 1e-9);
    CHECK_NEAR(cy, 0.0, 1e-9);
    CHECK_NEAR(r, 10.0, 1e-9);
    CHECK_NEAR(sw, M_PI / 2, 1e-9);

    pg_bulge_arc(10, 0, 0, 10, -pg_bulge_for_sweep(M_PI / 2), &cx, &cy, &r, &a0, &sw);
    CHECK_NEAR(cx, 10.0, 1e-9);
    CHECK_NEAR(cy, 10.0, 1e-9);

    PgVert circle[2] = { { 5, 0, 1.0 }, { -5, 0, 1.0 } };
    CHECK_NEAR(pg_path_area(circle, 2), M_PI * 25.0, 1e-9);
    CHECK_NEAR(pg_path_length(circle, 2, true), M_PI * 10.0, 1e-9);
    PgBox bx = pg_path_box(circle, 2, true);
    CHECK_NEAR(bx.minx, -5, 1e-9);
    CHECK_NEAR(bx.maxy, 5, 1e-9);
    CHECK_NEAR(bx.miny, -5, 1e-9);
}

static void test_rolled_cones_close(void)
{
    Build *b = build_new();
    build_run(b);

    double t = b->p.build.thickness_mm;
    int cones = 0, wraps = 0, tubes = 0;

    for (int i = 0; i < b->pl->n; i++) {
        const PgPart *q = &b->pl->part[i];
        if (q->kind == PG_PART_CONE) {
            cones++;
            CHECK(!q->mitred);
            double phi = q->sweep_deg * M_PI / 180.0;
            /* each rim unrolls to exactly its mean circumference */
            CHECK_NEAR(q->r_outer * phi, M_PI * q->d_large, 1e-6);
            CHECK_NEAR(q->r_inner * phi, M_PI * q->d_small, 1e-6);
            CHECK_NEAR(q->r_outer - q->r_inner, q->slant, 1e-9);
            double want = (q->r_outer + q->r_inner) * phi + 2.0 * q->slant;
            CHECK_NEAR(pg_path_length(q->v, q->n, true), want, 1e-6);
            CHECK_NEAR(q->area_mm2,
                       phi / 2.0 * (q->r_outer * q->r_outer - q->r_inner * q->r_inner),
                       1e-3);
        } else if (q->kind == PG_PART_WRAP) {
            wraps++;
            CHECK_NEAR(q->flat_w, M_PI * q->d_small, 1e-9);
        } else if (q->kind == PG_PART_TUBE) {
            tubes++;
        }
    }

    /* header 2 + diffuser 2x2 + baffle 2 cones; belly 2 wraps; stinger tube */
    CHECK(cones == 8);
    CHECK(wraps == 2);
    CHECK(tubes == 1);

    /* the first header piece starts where the cast duct ends */
    int s;
    double d_at_flange = pg_design_diameter_at(&b->d, b->p.engine.duct_len_mm, &s);
    CHECK_NEAR(b->pl->part[0].d_small, d_at_flange + t, 1e-9);
    CHECK(b->pl->part[0].piece == 1);
    CHECK(b->c->piece[1].part == 0);
    CHECK(b->pl->mass_kg > 0.0);
    build_free(b);
}

static void test_seam_allowance(void)
{
    Build *a = build_new(), *b = build_new();
    build_run(a);
    b->p.build.seam_allow_mm = 10.0;
    build_run(b);

    const PgPart *qa = &a->pl->part[2], *qb = &b->pl->part[2];
    CHECK(qa->kind == PG_PART_CONE);
    CHECK_NEAR(qb->area_mm2 - qa->area_mm2, 10.0 * qa->slant, qa->slant * 0.02);
    CHECK_NEAR(qb->r_outer, qa->r_outer, 1e-9);
    build_free(a);
    build_free(b);
}

/* Length of the 3D curve where a piece's surface meets one end plane. */
static double cut_curve_length(const PgPiece *pc, double rA, double rB, int end)
{
    const int N = 4000;
    double L = 0.0, prev[3];
    for (int i = 0; i <= N; i++) {
        double psi = 2.0 * M_PI * i / N, p[3];
        pg_piece_point(pc, rA, rB, psi, pg_piece_end_z(pc, rA, rB, psi, end), p);
        if (i) {
            double dd[3];
            v3_sub(dd, p, prev);
            L += v3_len(dd);
        }
        v3_copy(prev, p);
    }
    return L;
}

static double edge_length(const PgPart *q, int end)
{
    const int N = PG_MITRE_SAMPLES;
    double L = 0.0;
    for (int i = 0; i < N; i++) {
        int a = end ? (N + 1) + i : i;
        int b = a + 1;
        L += hypot(q->v[b].x - q->v[a].x, q->v[b].y - q->v[a].y);
    }
    return L;
}

static void test_mitred_development(void)
{
    Build *b = build_new();
    build_run(b);
    /* bend inside the second diffuser stage and inside the belly */
    int jd = -1, jb = -1;
    for (int i = 0; i < b->c->n_joints; i++) {
        const PgPiece *x = &b->c->piece[b->c->joint[i].before];
        const PgPiece *y = &b->c->piece[b->c->joint[i].after];
        if (x->section == 2 && y->section == 2) jd = i;
        if (x->section == 3 && y->section == 3) jb = i;
    }
    CHECK(jd >= 0 && jb >= 0);
    pg_route_set_bend(&b->p.route, b->c->joint[jd].x, 1.0, 30.0, 0.0);
    pg_route_set_bend(&b->p.route, b->c->joint[jb].x, 1.0, 40.0, 250.0);
    build_run(b);

    double t = b->p.build.thickness_mm;
    int checked = 0;
    for (int i = 0; i < b->pl->n; i++) {
        const PgPart *q = &b->pl->part[i];
        if (!q->mitred || q->kind == PG_PART_TUBE)
            continue;
        const PgPiece *pc = &b->c->piece[q->piece];
        double rA = (pc->d0 + t) / 2.0, rB = (pc->d1 + t) / 2.0;
        if (q->kind == PG_PART_WRAP)
            rB = rA;
        /* development is an isometry: each flat edge is as long as the 3D
         * curve it becomes (to the sampling of the flat edge) */
        for (int end = 0; end < 2; end++) {
            double want = cut_curve_length(pc, rA, rB, end);
            CHECK_NEAR(edge_length(q, end), want, want * 5e-4);
        }
        /* and every generator keeps its length */
        const int N = PG_MITRE_SAMPLES;
        double k = (rB - rA) / pc->len, g = sqrt(1 + k * k);
        for (int s = 0; s <= N; s += 12) {
            double psi = b->p.route.seam_deg * M_PI / 180.0 + 2.0 * M_PI * s / N;
            double z0 = pg_piece_end_z(pc, rA, rB, psi, 0);
            double z1 = pg_piece_end_z(pc, rA, rB, psi, 1);
            const PgVert *e0 = &q->v[s], *e1 = &q->v[2 * N + 1 - s];
            CHECK_NEAR(hypot(e1->x - e0->x, e1->y - e0->y), (z1 - z0) * g, 1e-6);
        }
        checked++;
    }
    CHECK(checked == 4);          /* two pieces either side of two joints */
    build_free(b);
}

static void test_hydroformed(void)
{
    Build *b = build_new();
    b->p.build.method = PG_MFG_HYDRO;
    b->p.build.hydro_margin_mm = 3.0;
    b->p.build.segments = 1;
    b->p.build.sheet_w_mm = 4000.0;         /* long enough for one piece */
    build_run(b);

    const PgPart *h = NULL;
    for (int i = 0; i < b->pl->n; i++)
        if (b->pl->part[i].kind == PG_PART_PILLOW)
            h = &b->pl->part[i];
    CHECK(h != NULL);
    if (!h) { build_free(b); return; }

    double t = b->p.build.thickness_mm;
    CHECK(h->qty == 2);

    /* sheet pieces: header remainder, 2 diffuser, belly, baffle => 6 stations */
    CHECK(h->n_st == 6);
    const PgPiece *first = &b->c->piece[1];
    CHECK_NEAR(h->st_hw[0], M_PI * (first->d0 + t) / 4.0 + 3.0, 1e-9);
    CHECK_NEAR(h->st_hw[3], M_PI * (b->d.d_belly + t) / 4.0 + 3.0, 1e-9);

    /* seam compensation: each flat stretch's edge is the inflated wall */
    for (int k = 1; k < h->n_st; k++) {
        const PgPiece *pc = &b->c->piece[k];
        double flat = hypot(h->st_x[k] - h->st_x[k - 1],
                            h->st_hw[k] - h->st_hw[k - 1]);
        double wall = hypot(pc->len, (pc->d1 - pc->d0) / 2.0);
        CHECK_NEAR(flat, wall, 1e-6);
    }

    double straight_area = h->area_mm2;

    /* an in-plane bend keeps the flat area to within the mitre's give */
    int j = 3;
    pg_route_set_bend(&b->p.route, b->c->joint[j].x, 1.0, 25.0, 90.0);
    build_run(b);
    for (int i = 0; i < b->pl->n; i++)
        if (b->pl->part[i].kind == PG_PART_PILLOW)
            h = &b->pl->part[i];
    CHECK(h->n_st == 6);
    CHECK_NEAR(h->area_mm2, straight_area, straight_area * 0.01);

    /* a short sheet splits the half into pieces that each fit */
    pg_project_default(&b->p);
    b->p.build.method = PG_MFG_HYDRO;
    b->p.build.sheet_w_mm = 1250.0;
    b->p.build.sheet_h_mm = 1000.0;
    build_run(b);
    int pieces = 0;
    for (int i = 0; i < b->pl->n; i++)
        if (b->pl->part[i].kind == PG_PART_PILLOW) {
            pieces++;
            CHECK(b->pl->part[i].flat_l <= 1250.0 - 2.0 * b->p.build.part_gap_mm + 1e-6);
        }
    CHECK(pieces >= 2);
    build_free(b);
}

static bool boxes_overlap(PgBox a, PgBox b)
{
    return a.minx < b.maxx && b.minx < a.maxx && a.miny < b.maxy && b.miny < a.maxy;
}

static PgBox placed_box(const PgPart *p, const PgPlace *pl)
{
    PgVert v[PG_PART_VERTS];
    for (int i = 0; i < p->n; i++) {
        pg_place_point(pl, p->v[i].x, p->v[i].y, &v[i].x, &v[i].y);
        v[i].bulge = p->v[i].bulge;
    }
    return pg_path_box(v, p->n, true);
}

static void test_layout(void)
{
    Build *b = build_new();
    b->p.engine.cylinders = 2;
    build_run(b);
    PgLayout *lay = calloc(1, sizeof *lay);
    pg_layout_build(b->pl, &b->p.build, lay);

    int want = 0;
    for (int i = 0; i < b->pl->n; i++)
        if (b->pl->part[i].kind != PG_PART_TUBE)
            want += b->pl->part[i].qty;
    CHECK(lay->n == want);
    CHECK(lay->n_unplaced == 0);
    CHECK(lay->n_oversize == 0);
    CHECK(lay->n_sheets >= 1);

    double g = b->p.build.part_gap_mm;
    for (int i = 0; i < lay->n; i++) {
        const PgPlace *a = &lay->place[i];
        PgBox ba = placed_box(&b->pl->part[a->part], a);
        CHECK(ba.minx >= g - 1e-6 && ba.miny >= g - 1e-6);
        CHECK(ba.maxx <= b->p.build.sheet_w_mm - g + 1e-6);
        CHECK(ba.maxy <= b->p.build.sheet_h_mm - g + 1e-6);
        for (int j = i + 1; j < lay->n; j++) {
            const PgPlace *c = &lay->place[j];
            if (c->sheet != a->sheet)
                continue;
            CHECK(!boxes_overlap(ba, placed_box(&b->pl->part[c->part], c)));
        }
    }

    b->p.build.sheet_w_mm = 300.0;
    b->p.build.sheet_h_mm = 300.0;
    pg_layout_build(b->pl, &b->p.build, lay);
    CHECK(lay->n == want);
    CHECK(lay->n_oversize > 0);

    free(lay);
    build_free(b);
}

TEST_MAIN("test_pattern",
    test_bulge_maths();
    test_rolled_cones_close();
    test_seam_allowance();
    test_mitred_development();
    test_hydroformed();
    test_layout();
)
