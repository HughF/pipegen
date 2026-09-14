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
 * test_route.c — bending must not change the chamber, only where it goes.
 */
#include "pg_test.h"
#include "pg_route.h"
#include "pg_vec.h"

#include <stdlib.h>

static PgChain *chain_for(const PgProject *p, PgDesign *d)
{
    PgChain *c = calloc(1, sizeof *c);
    pg_design_compute(p, d);
    pg_chain_build(p, d, c);
    return c;
}

static double centreline_length(const PgChain *c)
{
    double L = 0.0;
    for (int i = 0; i < c->n_pieces; i++) {
        double v[3];
        v3_sub(v, c->piece[i].p1, c->piece[i].p0);
        L += v3_len(v);
    }
    return L;
}

static void test_straight(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    PgChain *c = chain_for(&p, &d);

    /* duct, header 2, diffuser 2x2, belly 2, baffle 2, stinger tube */
    CHECK(c->n_pieces == 12);
    CHECK(c->piece[0].kind == PG_PIECE_DUCT);
    CHECK_NEAR(c->piece[0].len, 40.0, 1e-9);
    CHECK(c->piece[c->n_pieces - 1].kind == PG_PIECE_TUBE);
    CHECK(c->n_joints == c->n_pieces - 1);

    double sum = 0.0;
    for (int i = 0; i < c->n_pieces; i++) {
        sum += c->piece[i].len;
        CHECK_NEAR(c->piece[i].cut0_deg, 0.0, 1e-9);
        CHECK_NEAR(c->piece[i].cut1_deg, 0.0, 1e-9);
        if (i + 1 < c->n_pieces) {
            CHECK_NEAR(c->piece[i].x0 + c->piece[i].len, c->piece[i + 1].x0, 1e-9);
            CHECK_NEAR(c->piece[i].d1, c->piece[i + 1].d0, 1e-9);
        }
    }
    CHECK_NEAR(sum, d.total_len, 1e-6);

    /* straight out along +X from the port face */
    CHECK_NEAR(c->end[0], 40.0 + d.total_len, 1e-6);
    CHECK_NEAR(c->end[1], 0.0, 1e-9);
    CHECK_NEAR(c->end[2], 0.0, 1e-9);
    CHECK_NEAR(c->piece[0].ref[1], -1.0, 1e-12);          /* down */
    CHECK(c->n_clash_box + c->n_clash_engine + c->n_clash_self == 0);
    free(c);
}

static void test_bend_keeps_length(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    pg_design_compute(&p, &d);
    PgChain *c0 = chain_for(&p, &d);
    int j = 5;
    double at = c0->joint[j].x;
    free(c0);

    CHECK(pg_route_set_bend(&p.route, at, 1.0, 90.0, 0.0));
    PgChain *c = chain_for(&p, &d);
    CHECK_NEAR(c->joint[j].bend_deg, 90.0, 0);
    CHECK(c->joint[j].bend_index == 0);

    CHECK_NEAR(centreline_length(c), d.total_len, 1e-6);

    /* roll 0 turns down */
    const PgPiece *after = &c->piece[c->joint[j].after];
    CHECK_NEAR(after->axis[0], 0.0, 1e-12);
    CHECK_NEAR(after->axis[1], -1.0, 1e-12);

    /* frames stay orthonormal */
    for (int i = 0; i < c->n_pieces; i++) {
        const PgPiece *pc = &c->piece[i];
        CHECK_NEAR(v3_dot(pc->axis, pc->ref), 0.0, 1e-12);
        CHECK_NEAR(v3_dot(pc->axis, pc->side), 0.0, 1e-12);
        CHECK_NEAR(v3_len(pc->ref), 1.0, 1e-12);
    }

    /* 45 degree mitre on each side, leaning the right ways */
    const PgPiece *before = &c->piece[c->joint[j].before];
    CHECK_NEAR(before->cut1_deg, 45.0, 1e-9);
    CHECK_NEAR(after->cut0_deg, 45.0, 1e-9);
    CHECK_NEAR(before->bend1_roll, 0.0, 1e-9);

    /* the inside of the bend is shorter */
    double rA = before->d0 / 2, rB = before->d1 / 2;
    CHECK(pg_piece_end_z(before, rA, rB, 0.0, 1) <
          pg_piece_end_z(before, rA, rB, M_PI, 1));
    free(c);

    /* roll 180 turns up */
    pg_project_default(&p);
    pg_route_set_bend(&p.route, at, 1.0, 90.0, 180.0);
    c = chain_for(&p, &d);
    CHECK_NEAR(c->piece[c->joint[j].after].axis[1], 1.0, 1e-12);
    free(c);
}

/* Where two cylinders meet, both mitre cuts are the same ellipse. */
static void test_mitre_matches(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    pg_design_compute(&p, &d);
    PgChain *c0 = chain_for(&p, &d);

    int j = -1;
    for (int i = 0; i < c0->n_joints; i++)
        if (c0->piece[c0->joint[i].before].section == 3 &&
            c0->piece[c0->joint[i].after].section == 3)
            j = i;                                      /* inside the belly */
    CHECK(j >= 0);
    double at = c0->joint[j].x;
    free(c0);

    pg_route_set_bend(&p.route, at, 1.0, 35.0, 70.0);
    PgChain *c = chain_for(&p, &d);
    const PgPiece *a = &c->piece[c->joint[j].before];
    const PgPiece *b = &c->piece[c->joint[j].after];
    double r = a->d0 / 2.0;

    double worst = 0.0;
    for (int k = 0; k < 36; k++) {
        double psi = 2.0 * M_PI * k / 36;
        double pa[3], pb[3], dd[3];
        pg_piece_point(a, r, r, psi, pg_piece_end_z(a, r, r, psi, 1), pa);
        /* the same point on b: same direction in space, so find b's azimuth */
        double dir[3];
        v3_sub(dir, pa, c->joint[j].pos);
        double bpsi = atan2(v3_dot(dir, b->side), v3_dot(dir, b->ref));
        pg_piece_point(b, r, r, bpsi, pg_piece_end_z(b, r, r, bpsi, 0), pb);
        v3_sub(dd, pa, pb);
        if (v3_len(dd) > worst)
            worst = v3_len(dd);
    }
    CHECK(worst < 1e-6);
    free(c);
}

static void test_clashes(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    pg_design_compute(&p, &d);

    /* a small envelope: the straight chamber leaves it */
    p.clear.enabled = true;
    PgChain *c = chain_for(&p, &d);
    CHECK(c->n_clash_box > 0);
    free(c);

    /* ... a huge one it does not */
    for (int i = 0; i < 3; i++) {
        p.clear.min[i] = -10000.0;
        p.clear.max[i] = 10000.0;
    }
    c = chain_for(&p, &d);
    CHECK(c->n_clash_box == 0);
    free(c);

    /* as an obstacle, the huge box is hit */
    p.clear.keep_inside = false;
    c = chain_for(&p, &d);
    CHECK(c->n_clash_box > 0);
    free(c);

    /* two downward 90s early on send the chamber back under the engine */
    pg_project_default(&p);
    c = chain_for(&p, &d);
    double j1 = c->joint[1].x, j2 = c->joint[2].x;
    free(c);
    pg_route_set_bend(&p.route, j1, 1.0, 90.0, 0.0);
    pg_route_set_bend(&p.route, j2, 1.0, 90.0, 0.0);
    c = chain_for(&p, &d);
    CHECK(c->n_clash_engine > 0);
    CHECK_NEAR(centreline_length(c), d.total_len, 1e-6);
    free(c);

    /* A U-turn in the belly folds the chamber back onto itself: 94 mm
     * pieces against a 116 mm belly. (In the diffuser the legs of a U-turn
     * clear each other, which is how this test first found it was wrong.) */
    pg_project_default(&p);
    p.build.segments = 4;
    pg_design_compute(&p, &d);
    c = chain_for(&p, &d);
    int k = 0;
    while (k < c->n_joints && !(c->piece[c->joint[k].before].section == 3 &&
                                c->piece[c->joint[k].after].section == 3))
        k++;
    CHECK(k + 1 < c->n_joints);
    double u1 = c->joint[k].x, u2 = c->joint[k + 1].x;
    free(c);
    pg_route_set_bend(&p.route, u1, 1.0, 90.0, 90.0);
    pg_route_set_bend(&p.route, u2, 1.0, 90.0, 90.0);
    c = chain_for(&p, &d);
    CHECK(c->n_clash_self > 0);
    free(c);
}

static void test_bad_mitre_and_hydro(void)
{
    PgProject p;
    pg_project_default(&p);
    p.build.segments = 8;
    PgDesign d;
    pg_design_compute(&p, &d);
    PgChain *c = chain_for(&p, &d);
    /* two same-way 90s either side of a 36 mm header piece: its mitre cuts
     * cross on the inside of the turn */
    double a = c->joint[1].x, b = c->joint[2].x;
    free(c);
    pg_route_set_bend(&p.route, a, 1.0, 90.0, 0.0);
    pg_route_set_bend(&p.route, b, 1.0, 90.0, 0.0);
    c = chain_for(&p, &d);
    CHECK(c->piece[2].bad_mitre);
    CHECK(c->n_bad_mitre > 0);
    free(c);

    /* zig-zag the other way round and the same piece is fine */
    pg_project_default(&p);
    p.build.segments = 8;
    pg_route_set_bend(&p.route, a, 1.0, 90.0, 0.0);
    pg_route_set_bend(&p.route, b, 1.0, 90.0, 180.0);
    c = chain_for(&p, &d);
    CHECK(!c->piece[2].bad_mitre);
    free(c);

    /* hydroformed: a bend is turned into the plane of the seams */
    pg_project_default(&p);
    p.build.method = PG_MFG_HYDRO;
    pg_design_compute(&p, &d);
    c = chain_for(&p, &d);
    double at = c->joint[4].x;
    free(c);
    pg_route_set_bend(&p.route, at, 1.0, 30.0, 10.0);
    c = chain_for(&p, &d);
    CHECK_NEAR(c->joint[4].roll_deg, 90.0, 1e-9);
    CHECK(c->n_warn > 0);
    free(c);
}

TEST_MAIN("test_route",
    test_straight();
    test_bend_keeps_length();
    test_mitre_matches();
    test_clashes();
    test_bad_mitre_and_hydro();
)
