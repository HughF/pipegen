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
/* test_project.c — the project file round-trips and survives bad input. */
#include "pg_test.h"
#include "pg_project.h"

static void test_round_trip(void)
{
    PgProject a, b;
    pg_project_default(&a);
    snprintf(a.title, sizeof a.title, "Back\\slash and\nnewline");
    snprintf(a.notes, sizeof a.notes, "line one\nline two");
    a.build.method = PG_MFG_HYDRO;
    a.build.header_tube = true;
    a.tuning.objective = PG_OBJ_NOISE;
    a.geom.belly_ratio = 2.345678;
    a.engine.cylinders = 3;
    a.engine.duct_len_mm = 55.5;
    a.route.seam_deg = 90.0;
    CHECK(pg_route_set_bend(&a.route, 1000.0, 1.0, 30.0, 90.0));
    CHECK(pg_route_set_bend(&a.route, 1500.0, 1.0, 12.5, 270.0));
    a.clear.enabled = true;
    a.clear.keep_inside = false;
    a.clear.max[2] = 777.25;

    char t1[16384], t2[16384], err[160] = "";
    size_t n = pg_project_write(&a, t1, sizeof t1);
    CHECK(n > 0 && n < sizeof t1);
    CHECK(pg_project_parse(&b, t1, err, sizeof err));
    CHECK_STR(err, "");
    pg_project_write(&b, t2, sizeof t2);
    CHECK_STR(t2, t1);

    CHECK_STR(b.title, a.title);
    CHECK_STR(b.notes, a.notes);
    CHECK(b.build.method == PG_MFG_HYDRO);
    CHECK(b.build.header_tube);
    CHECK(b.tuning.objective == PG_OBJ_NOISE);
    CHECK_NEAR(b.geom.belly_ratio, 2.345678, 1e-9);
    CHECK(b.engine.cylinders == 3);
    CHECK_NEAR(b.engine.duct_len_mm, 55.5, 0);
    CHECK(b.route.n_bends == 2);
    CHECK_NEAR(b.route.bends[1].bend_deg, 12.5, 0);
    CHECK_NEAR(b.route.bends[1].roll_deg, 270.0, 0);
    CHECK(b.clear.enabled && !b.clear.keep_inside);
    CHECK_NEAR(b.clear.max[2], 777.25, 0);
}

static void test_bend_list(void)
{
    PgRoute r = { 0 };
    CHECK(pg_route_set_bend(&r, 500.0, 5.0, 20.0, 0.0));
    CHECK(pg_route_set_bend(&r, 503.0, 5.0, 30.0, 90.0));   /* same joint */
    CHECK(r.n_bends == 1);
    CHECK_NEAR(r.bends[0].bend_deg, 30.0, 0);
    CHECK(pg_route_find_bend(&r, 498.0, 5.0) == 0);
    CHECK(pg_route_find_bend(&r, 600.0, 5.0) == -1);
    CHECK(pg_route_set_bend(&r, 503.0, 5.0, 0.0, 0.0));     /* zero removes */
    CHECK(r.n_bends == 0);
    for (int i = 0; i < PG_MAX_BENDS; i++)
        CHECK(pg_route_set_bend(&r, 100.0 * (i + 1), 1.0, 5.0, 0.0));
    CHECK(!pg_route_set_bend(&r, 99999.0, 1.0, 5.0, 0.0));  /* full */
}

static void test_truncated_write_reports_length(void)
{
    PgProject a;
    pg_project_default(&a);
    char big[8192], small[16];
    size_t want = pg_project_write(&a, big, sizeof big);
    CHECK(pg_project_write(&a, small, sizeof small) == want);
    CHECK(strlen(small) == sizeof small - 1);
}

static void test_bad_input(void)
{
    PgProject p;
    char err[160] = "";
    CHECK(!pg_project_parse(&p, "hello", err, sizeof err));
    CHECK(err[0] != '\0');

    /* missing keys keep defaults; nonsense is clamped */
    err[0] = '\0';
    CHECK(pg_project_parse(&p,
        "# pipegen project\n"
        "engine.cylinders=99\n"
        "engine.bore_mm=nan\n"
        "geom.diffuser_stages=0\n"
        "tuning.objective=42\n"
        "unknown.key=1\n", err, sizeof err));
    CHECK(p.engine.cylinders == 8);
    CHECK(p.engine.bore_mm >= 10.0);
    CHECK(p.geom.diffuser_stages == 1);
    CHECK(p.tuning.objective < PG_OBJ_COUNT);
    CHECK_NEAR(p.engine.stroke_mm, 74.0, 0);

    /* a newer format still loads, with a note */
    err[0] = '\0';
    CHECK(pg_project_parse(&p, "# pipegen project\nformat=99\n", err, sizeof err));
    CHECK(strstr(err, "newer") != NULL);
}

static void test_file(void)
{
    PgProject a, b;
    pg_project_default(&a);
    a.tuning.rpm = 3123.0;
    char err[160] = "";
    const char *path = "build/testout/round.pgp";
    CHECK(pg_project_save(&a, path, err, sizeof err));
    CHECK(pg_project_load(&b, path, err, sizeof err));
    CHECK_NEAR(b.tuning.rpm, 3123.0, 0);
    CHECK(!pg_project_load(&b, "build/testout/does-not-exist.pgp", err, sizeof err));
}

TEST_MAIN("test_project",
    test_round_trip();
    test_bend_list();
    test_truncated_write_reports_length();
    test_bad_input();
    test_file();
)
