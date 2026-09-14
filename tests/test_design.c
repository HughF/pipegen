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
/* test_design.c — the wave-timing rules and the chamber they produce. */
#include "pg_test.h"
#include "pg_design.h"

static void test_default_engine(void)
{
    PgProject p;
    pg_project_default(&p);

    /* JLO L372: 80 x 74 mm */
    CHECK_NEAR(p.engine.bore_mm, 80.0, 0);
    CHECK_NEAR(p.engine.stroke_mm, 74.0, 0);
    CHECK_NEAR(pg_displacement_cc(&p.engine), 371.97, 0.01);
    CHECK_NEAR(p.tuning.rpm, 2850.0, 0);
    CHECK(p.tuning.objective == PG_OBJ_POWER_RPM);
}

static void test_wave_speed(void)
{
    /* 420 C is the gas temperature Jennings' 1700 ft/s (518 m/s) implies */
    CHECK_NEAR(pg_wave_speed(420.0), sqrt(1.35 * 287.0 * 693.15), 1e-9);
    CHECK_NEAR(pg_wave_speed(420.0), 518.2, 0.5);
}

static void test_tuned_length(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    pg_design_compute(&p, &d);

    double a = pg_wave_speed(420.0);
    double want = a * 155.0 / (12.0 * 2850.0) * 1000.0;
    CHECK_NEAR(d.tuned_len, want, 1e-6);
    CHECK_NEAR(d.tuned_len, 2349.0, 1.0);     /* the number on the design sheet */
    CHECK_NEAR(d.plug_dist, d.tuned_len, 1e-6);
    CHECK_NEAR(d.tuned_rpm, 2850.0, 1e-9);

    CHECK_NEAR(d.epo_deg, 102.5, 1e-9);
    CHECK_NEAR(d.epc_deg, 257.5, 1e-9);

    /* the whole point: the plugging pulse is back as the port closes */
    CHECK_NEAR(pg_return_angle(&d, d.plug_dist, 2850.0), d.epc_deg, 1e-6);

    /* and the band edges are where it is the window either side */
    CHECK_NEAR(pg_return_angle(&d, d.plug_dist, d.band_lo),
               d.epc_deg - PG_TIMING_WINDOW_DEG, 1e-6);
    CHECK_NEAR(pg_return_angle(&d, d.plug_dist, d.band_hi),
               d.epc_deg + PG_TIMING_WINDOW_DEG, 1e-6);
    CHECK(d.band_lo < 2850.0 && d.band_hi > 2850.0);
}

static void test_sections(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;
    pg_design_compute(&p, &d);

    CHECK(d.n_sec == 6);    /* header, 2 diffuser stages, belly, baffle, stinger */
    CHECK(d.sec[0].kind == PG_SEC_HEADER);
    CHECK(d.sec[d.n_sec - 1].kind == PG_SEC_STINGER);
    CHECK_NEAR(d.sec[0].x0, 0.0, 0);
    CHECK_NEAR(d.sec[0].d0, p.engine.outlet_dia_mm, 1e-9);

    for (int i = 0; i + 1 < d.n_sec; i++) {
        CHECK_NEAR(d.sec[i].x0 + d.sec[i].len, d.sec[i + 1].x0, 1e-9);
        CHECK_NEAR(d.sec[i].d1, d.sec[i + 1].d0, 1e-9);
    }
    const PgSection *last = &d.sec[d.n_sec - 1];
    CHECK_NEAR(d.total_len, last->x0 + last->len, 1e-9);

    CHECK_NEAR(d.d_belly, 38.0 * 3.0, 1e-9);
    CHECK_NEAR(d.d_stinger, 38.0 * 0.62, 1e-9);
    CHECK_NEAR(last->len, d.d_stinger * 12.0, 1e-9);

    /* equal-length stages, each steeper than the last */
    CHECK_NEAR(d.sec[1].len, d.sec[2].len, 1e-9);
    CHECK(pg_section_half_angle(&d.sec[2]) > pg_section_half_angle(&d.sec[1]));

    int s = -2;
    double mid = d.sec[3].x0 + d.sec[3].len / 2.0;
    CHECK_NEAR(pg_design_diameter_at(&d, mid, &s), d.d_belly, 1e-9);
    CHECK(s == 3);
    pg_design_diameter_at(&d, d.total_len + 1.0, &s);
    CHECK(s == -1);
}

static void test_presets_sum_to_tuned_length(void)
{
    for (int o = 0; o < PG_OBJ_COUNT; o++) {
        PgGeometry g;
        pg_geometry_preset((PgObjective)o, &g);
        CHECK_NEAR(g.f_header + g.f_diffuser + g.f_belly + g.f_baffle / 2.0,
                   1.0, 1e-9);
    }

    /* fractions that do not sum to 1 are normalised, not obeyed */
    PgProject p;
    pg_project_default(&p);
    p.geom.f_header *= 2.0;
    PgDesign d;
    pg_design_compute(&p, &d);
    CHECK_NEAR(d.plug_dist, d.tuned_len, 1e-6);
}

static void test_objectives(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;

    /* band: timed 60% of the way up */
    p.tuning.objective = PG_OBJ_POWER_BAND;
    p.tuning.rpm_lo = 2500.0;
    p.tuning.rpm_hi = 3500.0;
    pg_geometry_preset(p.tuning.objective, &p.geom);
    pg_design_compute(&p, &d);
    CHECK_NEAR(d.design_rpm, 3100.0, 1e-9);
    CHECK(d.n_sec == 7);                              /* three stages */

    /* economy: the plugging pulse 6 degrees early at the design speed */
    p.tuning.objective = PG_OBJ_ECONOMY;
    pg_geometry_preset(p.tuning.objective, &p.geom);
    pg_design_compute(&p, &d);
    CHECK_NEAR(pg_return_angle(&d, d.plug_dist, d.design_rpm),
               d.epc_deg - 6.0, 1e-6);
    CHECK(d.tuned_rpm > d.design_rpm);

    /* noise: gentler than power, with a silencer figure */
    PgProject q;
    pg_project_default(&q);
    PgDesign dp;
    pg_design_compute(&q, &dp);
    q.tuning.objective = PG_OBJ_NOISE;
    pg_geometry_preset(q.tuning.objective, &q.geom);
    pg_design_compute(&q, &d);
    CHECK(d.baffle_angle < dp.baffle_angle);
    CHECK(d.d_belly < dp.d_belly);
    CHECK_NEAR(d.silencer_litres, 20.0 * d.displacement_cc / 1000.0, 1e-9);
    CHECK_NEAR(dp.silencer_litres, 0.0, 0);
}

static void test_warnings(void)
{
    PgProject p;
    pg_project_default(&p);
    PgDesign d;

    p.geom.diffuser_stages = 1;
    p.geom.f_diffuser = 0.05;          /* a very short, steep diffuser */
    pg_design_compute(&p, &d);
    bool found = false;
    for (int i = 0; i < d.n_warn; i++)
        if (strstr(d.warn[i], "Diffuser half-angle"))
            found = true;
    CHECK(found);

    pg_project_default(&p);
    p.engine.exh_duration_deg = 240.0;
    pg_design_compute(&p, &d);
    found = false;
    for (int i = 0; i < d.n_warn; i++)
        if (strstr(d.warn[i], "Exhaust duration"))
            found = true;
    CHECK(found);
}

TEST_MAIN("test_design",
    test_default_engine();
    test_wave_speed();
    test_tuned_length();
    test_sections();
    test_presets_sum_to_tuned_length();
    test_objectives();
    test_warnings();
)
