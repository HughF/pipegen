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
 * pg_autofold.h — fold a long chamber into a safe, compact layout
 *
 * A layout is described as a handful of turns, each spread evenly over a run
 * of consecutive joints with one direction, so every turn is a smooth planar
 * arc of mitred pieces — the way a builder would bend it. The search looks
 * for the best such layout that breaks no rule.
 *
 * Safe — every rule is a hard constraint:
 *   - no joint sharper than its section allows, and no run of bends tighter
 *     than PG_MIN_BEND_RADIUS_D diameters (the rules in pg_route.h);
 *   - no piece too short for the mitres at its two ends;
 *   - at least the heat gap from the engine and from other parts of the
 *     chamber, which change the gas temperature and so the tuning;
 *   - inside (or outside) the clearance box by the wall gap, when one is set.
 *
 * Best — among safe layouts, the lowest score of:
 *   - most compact: the size of the whole package, engine included;
 *     fit the box: the margin to the box and the engine (more is better);
 *   - plus the amount of bending, weighted heavier in the fat belly and
 *     baffle, the number of turns and joints bent, and any extra pieces.
 *
 * The search is seeded simulated annealing over the turns, repeated for each
 * number of turns and each allowed segment count, then refined. It uses a
 * fast, deliberately conservative geometric model (every piece as a capsule
 * of its largest radius, mitre ends as spheres enlarged by the cut); the
 * winner is then checked again with the chain's full surface check before it
 * is called safe. It runs in slices, so the interface can show progress.
 *
 * No SDL: the tests and the command line use it directly.
 */
#ifndef PG_AUTOFOLD_H
#define PG_AUTOFOLD_H

#include <stdbool.h>
#include "pg_project.h"

typedef enum {
    PG_FOLD_COMPACT = 0,   /* smallest package                          */
    PG_FOLD_FIT_BOX        /* inside (or outside) the clearance box      */
} PgFoldGoal;

typedef enum {
    PG_FOLD_ANY = 0,       /* turns in any direction                    */
    PG_FOLD_FLAT,          /* sideways only: stays at port height        */
    PG_FOLD_UPRIGHT        /* up and down only: stays in one vertical plane */
} PgFoldPlane;

typedef struct {
    PgFoldGoal  goal;
    PgFoldPlane plane;
    int    max_turns;      /* 1..4                                      */
    int    max_segments;   /* may raise segments per section up to this */
    double heat_gap_mm;    /* from the engine and from itself           */
    double wall_gap_mm;    /* from the clearance box                    */
    int    effort;         /* 1 quick, 2 normal, 3 thorough             */
    unsigned seed;
} PgFoldOpts;

typedef struct {
    bool    ok;            /* safe by every rule, checked in full       */
    PgRoute route;         /* the bends; seam position kept             */
    int     segments;
    int     turns;
    int     bent_joints;
    double  total_turn_deg;
    double  sharpest;      /* worst joint angle / its limit (0..1 safe) */
    double  tightest_d;    /* tightest run of bends, diameters          */
    double  min_gap_mm;    /* smallest gap to anything checked          */
    double  envelope[3];   /* chamber, mm                               */
    long    evaluations;
    char    summary[480];
} PgFoldResult;

void pg_fold_defaults(PgFoldOpts *o, const PgProject *p);

typedef struct PgFolder PgFolder;

PgFolder *pg_fold_begin(const PgProject *p, const PgFoldOpts *o);

/* Run up to `evals` evaluations. True once the search has finished and the
 * result is final. */
bool pg_fold_step(PgFolder *f, int evals);

double pg_fold_progress(const PgFolder *f);            /* 0..1 */
const PgFoldResult *pg_fold_result(const PgFolder *f); /* final when done */
void pg_fold_end(PgFolder *f);

/* The whole search in one call. False if no safe layout was found; `out`
 * then describes the closest attempt. */
bool pg_autofold(const PgProject *p, const PgFoldOpts *o, PgFoldResult *out);

/* Replace the project's bends and segment count with the result's. */
void pg_fold_apply(PgProject *p, const PgFoldResult *r);

#endif /* PG_AUTOFOLD_H */
