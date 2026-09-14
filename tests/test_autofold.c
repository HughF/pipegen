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
 * test_autofold.c — the bend rules, and a fold that keeps them.
 *
 * Every result auto-fold calls safe is rebuilt here and checked with the
 * chain's own full surface checks, independently of the optimiser's fast
 * model. A fold that only looks safe to the search does not pass.
 */
#include "pg_test.h"
#include "pg_autofold.h"
#include "pg_design.h"
#include "pg_route.h"

#include <stdlib.h>

static PgChain *built(const PgProject *p)
{
    PgDesign d;
    pg_design_compute(p, &d);
    PgChain *c = calloc(1, sizeof *c);
    pg_chain_build(p, &d, c);
    return c;
}

static double longest_side(const PgChain *c)
{
    double m = 0.0;
    for (int a = 0; a < 3; a++)
        m = fmax(m, c->bmax[a] - c->bmin[a]);
    return m;
}

/* A fold result, rebuilt and checked in full. */
static void check_safe(const PgProject *p, const PgFoldResult *r, bool box)
{
    PgProject q = *p;
    pg_fold_apply(&q, r);
    PgChain *c = built(&q);
    PgDesign d;
    pg_design_compute(&q, &d);

    CHECK(c->n_sharp == 0);
    CHECK(c->n_bad_mitre == 0);
    CHECK(c->n_clash_engine == 0);
    CHECK(c->n_clash_self == 0);
    if (box)
        CHECK(c->n_clash_box == 0);

    for (int j = 0; j < c->n_joints; j++)
        CHECK(c->joint[j].bend_deg <= c->joint[j].limit_deg + 1e-9);

    /* bending never changes the centreline */
    double L = 0.0;
    for (int i = 0; i < c->n_pieces; i++)
        L += c->piece[i].len;
    CHECK_NEAR(L, d.total_len, 1e-6);
    free(c);
}

static void test_rules(void)
{
    PgProject p;
    pg_project_default(&p);
    p.build.segments = 4;
    PgChain *c = built(&p);

    int belly = -1;
    for (int j = 0; j < c->n_joints; j++)
        if (belly < 0 && c->piece[c->joint[j].before].sec_kind == PG_SEC_BELLY &&
            c->piece[c->joint[j].after].sec_kind == PG_SEC_BELLY)
            belly = j;
    CHECK(belly >= 0);
    CHECK_NEAR(pg_joint_limit(c, belly), 20.0, 0);
    CHECK_NEAR(pg_joint_limit(c, 0), 30.0, 0);               /* duct -> header */
    CHECK_NEAR(pg_joint_limit(c, c->n_joints - 1), 15.0, 0);  /* into the stinger */
    double x0 = c->joint[belly].x, x1 = c->joint[belly + 1].x,
           x2 = c->joint[belly + 2].x;
    free(c);

    /* one 45 degree mitre in the belly is sharp */
    PgProject q = p;
    pg_route_set_bend(&q.route, x0, 1.0, 45.0, 0.0);
    c = built(&q);
    CHECK(c->joint[belly].sharp);
    CHECK(c->n_sharp == 1);
    CHECK(c->n_warn > 0 && strstr(c->warn[c->n_warn - 1], "too sharply") != NULL);
    free(c);

    /* the same turn as three 15s on 94 mm pieces is a 3-diameter run: fine */
    q = p;
    pg_route_set_bend(&q.route, x0, 1.0, 15.0, 0.0);
    pg_route_set_bend(&q.route, x1, 1.0, 15.0, 0.0);
    pg_route_set_bend(&q.route, x2, 1.0, 15.0, 0.0);
    c = built(&q);
    CHECK(c->n_sharp == 0);
    CHECK(pg_piece_bend_radius_d(c, belly + 1) > 2.0);
    free(c);

    /* ...but 20s on 47 mm pieces make a run of about one diameter: too tight */
    q = p;
    q.build.segments = 8;
    c = built(&q);
    int j8 = -1;
    for (int j = 0; j < c->n_joints; j++)
        if (j8 < 0 && c->piece[c->joint[j].before].sec_kind == PG_SEC_BELLY &&
            c->piece[c->joint[j].after].sec_kind == PG_SEC_BELLY)
            j8 = j;
    double a = c->joint[j8].x, b = c->joint[j8 + 1].x;
    free(c);
    pg_route_set_bend(&q.route, a, 1.0, 20.0, 0.0);
    pg_route_set_bend(&q.route, b, 1.0, 20.0, 0.0);
    c = built(&q);
    CHECK(pg_piece_bend_radius_d(c, j8 + 1) < 2.0);
    CHECK(c->n_sharp == 2);
    free(c);
}

static void test_compact(void)
{
    PgProject p;
    pg_project_default(&p);
    PgFoldOpts o;
    pg_fold_defaults(&o, &p);
    CHECK(o.goal == PG_FOLD_COMPACT);
    o.effort = 1;
    o.max_segments = 5;

    PgFoldResult r;
    bool ok = pg_autofold(&p, &o, &r);
    printf("  compact: %s\n", r.summary);
    CHECK(ok);
    CHECK(r.bent_joints > 0);
    CHECK(r.sharpest <= 1.0 + 1e-9);
    CHECK(r.tightest_d >= PG_MIN_BEND_RADIUS_D - 1e-9);
    CHECK(r.min_gap_mm >= o.heat_gap_mm - 1e-6);
    check_safe(&p, &r, false);

    PgChain *straight = built(&p);
    PgProject q = p;
    pg_fold_apply(&q, &r);
    PgChain *folded = built(&q);
    printf("  longest side %.0f mm straight, %.0f mm folded\n",
           longest_side(straight), longest_side(folded));
    CHECK(longest_side(folded) < 0.7 * longest_side(straight));
    free(straight);
    free(folded);

    /* the same seed gives the same fold */
    PgFoldResult r2;
    pg_autofold(&p, &o, &r2);
    CHECK(r2.route.n_bends == r.route.n_bends);
    CHECK(r2.segments == r.segments);
    CHECK(memcmp(r2.route.bends, r.route.bends,
                 (size_t)r.route.n_bends * sizeof r.route.bends[0]) == 0);
}

static void test_fit_box(void)
{
    PgProject p;
    pg_project_default(&p);
    p.clear.enabled = true;
    p.clear.keep_inside = true;
    p.clear.min[0] = -300.0;  p.clear.max[0] = 1700.0;
    p.clear.min[1] = -500.0;  p.clear.max[1] = 600.0;
    p.clear.min[2] = -550.0;  p.clear.max[2] = 550.0;

    PgChain *c = built(&p);
    CHECK(c->n_clash_box > 0);                 /* straight, it does not fit */
    free(c);

    PgFoldOpts o;
    pg_fold_defaults(&o, &p);
    CHECK(o.goal == PG_FOLD_FIT_BOX);
    o.effort = 1;
    o.max_segments = 5;
    PgFoldResult r;
    bool ok = pg_autofold(&p, &o, &r);
    printf("  fit box: %s\n", r.summary);
    CHECK(ok);
    check_safe(&p, &r, true);
}

static void test_impossible(void)
{
    PgProject p;
    pg_project_default(&p);
    p.clear.enabled = true;
    p.clear.keep_inside = true;
    for (int a = 0; a < 3; a++) {
        p.clear.min[a] = -300.0;
        p.clear.max[a] = 300.0;
    }
    PgFoldOpts o;
    pg_fold_defaults(&o, &p);
    o.effort = 1;
    o.max_turns = 2;
    PgFoldResult r;
    CHECK(!pg_autofold(&p, &o, &r));
    CHECK(strstr(r.summary, "No safe fold") != NULL);
    CHECK(strstr(r.summary, "box") != NULL);

    /* asked to fit a box that is not set: folded for compactness, and said */
    pg_project_default(&p);
    pg_fold_defaults(&o, &p);
    o.goal = PG_FOLD_FIT_BOX;
    o.effort = 1;
    o.max_segments = p.build.segments;
    pg_autofold(&p, &o, &r);
    CHECK(strstr(r.summary, "No clearance box is set") != NULL);
}

static void test_hydro_in_plane(void)
{
    PgProject p;
    pg_project_default(&p);
    p.build.method = PG_MFG_HYDRO;
    PgFoldOpts o;
    pg_fold_defaults(&o, &p);
    o.effort = 1;
    o.max_segments = 4;
    o.plane = PG_FOLD_UPRIGHT;          /* overridden: hydro folds in its seams */
    PgFoldResult r;
    bool ok = pg_autofold(&p, &o, &r);
    printf("  hydroformed: %s\n", r.summary);
    CHECK(ok);
    for (int i = 0; i < r.route.n_bends; i++) {
        double roll = r.route.bends[i].roll_deg;
        CHECK(fabs(roll - 90.0) < 1e-6 || fabs(roll - 270.0) < 1e-6);
    }
    check_safe(&p, &r, false);
}

static void test_steps(void)
{
    PgProject p;
    pg_project_default(&p);
    PgFoldOpts o;
    pg_fold_defaults(&o, &p);
    o.effort = 1;
    o.max_segments = p.build.segments;
    o.max_turns = 1;

    PgFolder *f = pg_fold_begin(&p, &o);
    CHECK(f != NULL);
    double last = pg_fold_progress(f);
    CHECK(last < 0.05);
    bool monotone = true;
    int steps = 0;
    while (!pg_fold_step(f, 50)) {
        double now = pg_fold_progress(f);
        if (now + 1e-12 < last)
            monotone = false;
        last = now;
        steps++;
    }
    CHECK(monotone);
    CHECK(steps > 3);
    CHECK_NEAR(pg_fold_progress(f), 1.0, 0);
    CHECK(pg_fold_result(f)->evaluations > 0);
    pg_fold_end(f);
}

TEST_MAIN("test_autofold",
    test_rules();
    test_compact();
    test_fit_box();
    test_impossible();
    test_hydro_in_plane();
    test_steps();
)
