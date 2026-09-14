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
#include "pg_autofold.h"
#include "pg_design.h"
#include "pg_route.h"
#include "pg_vec.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DEG (M_PI / 180.0)
#define MAX_TURNS 4
#define MAX_SEGMENTS 8
#define SAMPLES 9                 /* points along a piece for box and engine */

/* An infeasible layout always scores worse than any feasible one, but still
 * by how far it breaks the rules, so the search can climb out. */
#define INFEASIBLE 5000.0

typedef struct {
    int    start, span;           /* joints                                  */
    double angle;                 /* total, degrees, shared along the span   */
    double roll;
} Turn;

typedef struct {
    int  k;
    Turn t[MAX_TURNS];
} Cand;

typedef struct {
    double pen;                   /* weighted rule breaking; 0 = safe        */
    double box_mm, engine_mm, self_mm, angle_deg, radius_d, mitre_mm;
    double min_gap;
    double ext[3];
    double raw_turn, weighted_turn, sharpest, tightest;
    double worst_box, worst_engine, worst_self;   /* largest single shortfall */
    int    bent, turns, pieces;
} Eval;

struct PgFolder {
    PgProject  base;              /* the project, bends removed              */
    PgProject  work;
    PgFoldOpts o;
    bool       box_fallback;      /* asked to fit a box that is not set      */
    PgDesign   d;
    PgChain   *c;
    int        base_pieces;

    int    segs[MAX_SEGMENTS], n_segs, seg_i;
    int    nj;
    double jx[PG_MAX_PIECES];
    int    decoded_turns;

    int    k, run, runs, iter, iters;
    bool   refining;
    int    refine_iters;

    Cand   cur;
    double cur_cost;
    Cand   best;
    double best_cost;
    int    best_seg;

    uint64_t rng;
    long   evals, planned;
    bool   finished;
    PgFoldResult res;
};

/* ---- random ------------------------------------------------------------ */

static uint64_t rnext(PgFolder *f)
{
    uint64_t x = f->rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return f->rng = x;
}

static double urand(PgFolder *f)
{
    return (double)(rnext(f) >> 11) * (1.0 / 9007199254740992.0);
}

static int irand(PgFolder *f, int n)
{
    return n > 0 ? (int)(urand(f) * n) % n : 0;
}

static double nrand(PgFolder *f)
{
    double u = urand(f), v = urand(f);
    return sqrt(-2.0 * log(u + 1e-12)) * cos(2.0 * M_PI * v);
}

/* ---- directions -------------------------------------------------------- */

/* The rolls a turn may use: a pair when the direction is constrained, none
 * (any roll) otherwise. A hydroformed chamber is always constrained to the
 * plane of its seams, whatever was asked. */
static int allowed_rolls(const PgFolder *f, double out[2])
{
    if (f->base.build.method == PG_MFG_HYDRO) {
        out[0] = fmod(f->base.route.seam_deg + 90.0, 360.0);
        out[1] = fmod(f->base.route.seam_deg + 270.0, 360.0);
        return 2;
    }
    switch (f->o.plane) {
    case PG_FOLD_FLAT:    out[0] = 90.0; out[1] = 270.0; return 2;
    case PG_FOLD_UPRIGHT: out[0] = 0.0;  out[1] = 180.0; return 2;
    default:              return 0;
    }
}

static double pick_roll(PgFolder *f)
{
    double pair[2];
    if (allowed_rolls(f, pair))
        return pair[irand(f, 2)];
    return urand(f) < 0.5 ? 90.0 * irand(f, 4) : 360.0 * urand(f);
}

/* ---- candidates --------------------------------------------------------- */

static void setup_segments(PgFolder *f, int segments)
{
    f->work = f->base;
    f->work.build.segments = segments;
    f->work.route.n_bends = 0;
    pg_chain_layout(&f->work, &f->d, f->c);
    f->nj = f->c->n_joints;
    for (int j = 0; j < f->nj; j++)
        f->jx[j] = f->c->joint[j].x;
}

static void clamp_turn(const PgFolder *f, Turn *t)
{
    if (f->nj <= 0) {
        t->span = 0;
        return;
    }
    if (t->start < 0) t->start = 0;
    if (t->start > f->nj - 1) t->start = f->nj - 1;
    if (t->span < 1) t->span = 1;
    if (t->span > f->nj) t->span = f->nj;
    if (t->angle < 0.0) t->angle = 0.0;
    if (t->angle > 270.0) t->angle = 270.0;
    t->roll = fmod(t->roll, 360.0);
    if (t->roll < 0.0) t->roll += 360.0;
}

/* Turns into bends on the work project: sorted by start, never overlapping,
 * each angle shared evenly along its joints. */
static void decode(PgFolder *f, const Cand *cd)
{
    Turn t[MAX_TURNS];
    int k = cd->k;
    memcpy(t, cd->t, (size_t)k * sizeof t[0]);
    for (int i = 1; i < k; i++)
        for (int j = i; j > 0 && t[j].start < t[j - 1].start; j--) {
            Turn s = t[j];
            t[j] = t[j - 1];
            t[j - 1] = s;
        }

    PgRoute *r = &f->work.route;
    r->n_bends = 0;
    f->decoded_turns = 0;
    int end = 0;
    for (int i = 0; i < k; i++) {
        int s = t[i].start > end ? t[i].start : end;
        int span = t[i].span;
        if (s >= f->nj)
            break;
        if (s + span > f->nj)
            span = f->nj - s;
        if (span < 1 || t[i].angle < 0.5)
            continue;
        double per = t[i].angle / span;
        for (int j = s; j < s + span && r->n_bends < PG_MAX_BENDS; j++) {
            r->bends[r->n_bends].at_mm = f->jx[j];
            r->bends[r->n_bends].bend_deg = per;
            r->bends[r->n_bends].roll_deg = t[i].roll;
            r->n_bends++;
        }
        f->decoded_turns++;
        end = s + span;
    }
}

/* How hard bending a joint is on the pipe: fat sections suffer most. */
static double joint_weight(const PgChain *c, int j)
{
    static const double w[] = { 0.6, 1.0, 1.4, 1.4, 1.6 };
    const PgJoint *jt = &c->joint[j];
    int a = (int)c->piece[jt->before].sec_kind, b = (int)c->piece[jt->after].sec_kind;
    return fmax(w[a < 5 ? a : 4], w[b < 5 ? b : 4]);
}

static double point_box_dist(const double *p, const double *mn, const double *mx)
{
    double d2 = 0.0, inside = HUGE_VAL;
    bool in = true;
    for (int i = 0; i < 3; i++) {
        double lo = mn[i] - p[i], hi = p[i] - mx[i];
        if (lo > 0.0) { d2 += lo * lo; in = false; }
        else if (hi > 0.0) { d2 += hi * hi; in = false; }
        else inside = fmin(inside, fmin(-lo, -hi));
    }
    return in ? -inside : sqrt(d2);
}

/* Signed distance to the engine: the finned barrel and head as one vertical
 * cylinder, and the crankcase box. */
static double engine_dist(const PgStub *s, const double *p)
{
    double radial = hypot(p[0], p[2]) - s->barrel_r;
    double below = s->barrel_y0 - p[1], above = p[1] - s->head_y1;
    double vert = fmax(0.0, fmax(below, above));
    double dc;
    if (radial <= 0.0 && vert <= 0.0)
        dc = -fmin(-radial, fmin(-below, -above));
    else
        dc = hypot(fmax(radial, 0.0), vert);
    return fmin(dc, point_box_dist(p, s->case_min, s->case_max));
}

static double evaluate(PgFolder *f, const Cand *cd, Eval *e)
{
    memset(e, 0, sizeof *e);
    e->min_gap = HUGE_VAL;
    e->tightest = HUGE_VAL;
    f->evals++;

    decode(f, cd);
    PgChain *c = f->c;
    const PgProject *p = &f->work;
    const PgFoldOpts *o = &f->o;
    pg_chain_layout(p, &f->d, c);
    e->turns = f->decoded_turns;

    /* ---- the bend rules ---- */
    for (int j = 0; j < c->n_joints; j++) {
        double b = c->joint[j].bend_deg;
        if (b < 1e-6)
            continue;
        e->bent++;
        e->raw_turn += b;
        e->weighted_turn += b * joint_weight(c, j);
        double lim = c->joint[j].limit_deg;
        e->sharpest = fmax(e->sharpest, b / lim);
        if (b > lim)
            e->angle_deg += b - lim;
    }
    for (int i = 1; i + 1 < c->n_pieces; i++) {
        double rr = pg_piece_bend_radius_d(c, i);
        e->tightest = fmin(e->tightest, rr);
        if (rr < PG_MIN_BEND_RADIUS_D)
            e->radius_d += PG_MIN_BEND_RADIUS_D - rr;
    }

    /* ---- the geometry ---- */
    double t = p->build.thickness_mm;
    const PgStub *st = &c->stub;
    double mn[3] = { -st->barrel_r, st->case_min[1], st->case_min[2] };
    double mx[3] = { st->barrel_r, st->head_y1, st->case_max[2] };

    int first_made = -1;
    for (int i = 0; i < c->n_pieces; i++)
        if (c->piece[i].kind != PG_PIECE_DUCT) {
            first_made = i;
            break;
        }

    for (int i = 0; i < c->n_pieces; i++) {
        const PgPiece *pc = &c->piece[i];
        if (pc->kind == PG_PIECE_DUCT)
            continue;
        e->pieces++;
        double r = fmax(pc->d0, pc->d1) / 2.0 + t;
        double c0 = fmax(0.5, cos(pc->cut0_deg * DEG));
        double c1 = fmax(0.5, cos(pc->cut1_deg * DEG));
        double re[2] = { r / c0, r / c1 };
        const double *ends[2] = { pc->p0, pc->p1 };
        double rmax = fmax(re[0], re[1]);

        double need = r * (tan(pc->cut0_deg * DEG) + tan(pc->cut1_deg * DEG)) + 5.0;
        if (pc->len < need)
            e->mitre_mm += need - pc->len;

        for (int k = 0; k < 2; k++)
            for (int a = 0; a < 3; a++) {
                mn[a] = fmin(mn[a], ends[k][a] - re[k]);
                mx[a] = fmax(mx[a], ends[k][a] + re[k]);
            }

        if (p->clear.enabled && o->goal == PG_FOLD_FIT_BOX) {
            if (p->clear.keep_inside) {
                /* a straight piece is inside a box when both ends are */
                for (int k = 0; k < 2; k++)
                    for (int a = 0; a < 3; a++) {
                        double lo = ends[k][a] - re[k] - (p->clear.min[a] + o->wall_gap_mm);
                        double hi = (p->clear.max[a] - o->wall_gap_mm) - (ends[k][a] + re[k]);
                        double g = fmin(lo, hi);
                        e->min_gap = fmin(e->min_gap, g + o->wall_gap_mm);
                        if (g < 0.0) {
                            e->box_mm += -g;
                            e->worst_box = fmax(e->worst_box, -g);
                        }
                    }
            } else {
                double worst = 0.0;
                for (int s = 0; s < SAMPLES; s++) {
                    double q[3];
                    v3_add_scaled(q, pc->p0, pc->axis, pc->len * s / (SAMPLES - 1));
                    double g = point_box_dist(q, p->clear.min, p->clear.max) - rmax;
                    e->min_gap = fmin(e->min_gap, g);
                    worst = fmax(worst, o->wall_gap_mm - g);
                }
                e->box_mm += worst;
                e->worst_box = fmax(e->worst_box, worst);
            }
        }

        if (i > first_made) {
            double worst = 0.0;
            for (int s = 0; s < SAMPLES; s++) {
                double q[3];
                v3_add_scaled(q, pc->p0, pc->axis, pc->len * s / (SAMPLES - 1));
                double g = engine_dist(st, q) - rmax;
                e->min_gap = fmin(e->min_gap, g);
                worst = fmax(worst, o->heat_gap_mm - g);
            }
            e->engine_mm += worst;
            e->worst_engine = fmax(e->worst_engine, worst);
        }
    }

    /* the chamber against itself, as capsules */
    for (int i = 0; i < c->n_pieces; i++) {
        const PgPiece *a = &c->piece[i];
        if (a->kind == PG_PIECE_DUCT)
            continue;
        for (int j = i + 2; j < c->n_pieces; j++) {
            const PgPiece *b = &c->piece[j];
            double sa, sb;
            double dist = pg_segment_distance(a->p0, a->p1, b->p0, b->p1, &sa, &sb);
            double ra = fmax(a->d0, a->d1) / 2.0 + t;
            double rb = fmax(b->d0, b->d1) / 2.0 + t;
            double along = (b->x0 + sb * b->len) - (a->x0 + sa * a->len);
            if (along <= 1.5 * (ra + rb) + o->heat_gap_mm)
                continue;
            double g = dist - ra - rb;
            e->min_gap = fmin(e->min_gap, g);
            if (g < o->heat_gap_mm) {
                e->self_mm += o->heat_gap_mm - g;
                e->worst_self = fmax(e->worst_self, o->heat_gap_mm - g);
            }
        }
    }

    for (int a = 0; a < 3; a++)
        e->ext[a] = mx[a] - mn[a];

    e->pen = 20.0 * e->angle_deg + 150.0 * e->radius_d + 4.0 * e->mitre_mm +
             4.0 * (e->box_mm + e->engine_mm + e->self_mm);

    double obj = 0.35 * e->weighted_turn + 25.0 * e->turns + 5.0 * e->bent +
                 6.0 * (e->pieces - f->base_pieces);
    if (o->goal == PG_FOLD_COMPACT) {
        double vol = e->ext[0] * e->ext[1] * e->ext[2];
        obj += cbrt(vol) + 0.25 * fmax(e->ext[0], fmax(e->ext[1], e->ext[2]));
    } else if (isfinite(e->min_gap)) {
        obj -= 1.5 * fmin(e->min_gap, 60.0);
    }

    return obj + (e->pen > 0.0 ? INFEASIBLE + 50.0 * e->pen : 0.0);
}

static void consider(PgFolder *f, const Cand *cd, double cost)
{
    if (cost < f->best_cost) {
        f->best = *cd;
        f->best_cost = cost;
        f->best_seg = f->work.build.segments;
    }
}

/* The first two runs of each turn count start from shapes a builder would
 * try — evenly spaced turns, then the same with alternating directions (an
 * S, or a zig-zag) — and the rest start at random. */
static void init_candidate(PgFolder *f, Cand *cd)
{
    memset(cd, 0, sizeof *cd);
    cd->k = f->k;
    double pair[2];
    int np = allowed_rolls(f, pair);
    for (int i = 0; i < f->k; i++) {
        Turn *t = &cd->t[i];
        if (f->run < 2) {
            t->angle = f->k == 1 ? 180.0 : 120.0;
            t->span = (int)ceil(t->angle / 18.0) + 1;
            if (t->span > f->nj / f->k)
                t->span = f->nj / f->k;
            int centre = (i + 1) * f->nj / (f->k + 1);
            t->start = centre - t->span / 2;
            double base = np ? pair[0] : 90.0;
            double other = np ? pair[1] : 270.0;
            t->roll = (f->run == 1 && (i % 2)) ? other : base;
        } else {
            t->start = irand(f, f->nj);
            t->span = 1 + irand(f, 10);
            t->angle = 30.0 + 150.0 * urand(f);
            t->roll = pick_roll(f);
        }
        clamp_turn(f, t);
    }
}

static void mutate(PgFolder *f, const Cand *in, Cand *out)
{
    *out = *in;
    if (out->k == 0)
        return;
    Turn *t = &out->t[irand(f, out->k)];
    double scale = f->refining ? 0.3 : 1.0;
    double u = urand(f);
    double pair[2];
    int np = allowed_rolls(f, pair);

    if (!f->refining && urand(f) < 0.04) {
        t->start = irand(f, f->nj);
        t->span = 1 + irand(f, 10);
        t->angle = 30.0 + 150.0 * urand(f);
        t->roll = pick_roll(f);
    } else if (u < 0.2) {
        t->start += (irand(f, 2) ? 1 : -1) * (1 + irand(f, f->refining ? 1 : 3));
    } else if (u < 0.4) {
        t->span += irand(f, 2) ? 1 : -1;
    } else if (u < 0.75) {
        t->angle += nrand(f) * 25.0 * scale;
    } else if (np) {
        if (urand(f) < 0.5)
            t->roll = (fabs(remainder(t->roll - pair[0], 360.0)) < 1.0) ? pair[1] : pair[0];
    } else {
        t->roll += nrand(f) * 40.0 * scale;
        if (urand(f) < 0.1)
            t->roll = 90.0 * irand(f, 4);
    }
    clamp_turn(f, t);
}

/* ---- schedule ---------------------------------------------------------- */

void pg_fold_defaults(PgFoldOpts *o, const PgProject *p)
{
    memset(o, 0, sizeof *o);
    o->goal = (p && p->clear.enabled) ? PG_FOLD_FIT_BOX : PG_FOLD_COMPACT;
    o->plane = PG_FOLD_ANY;
    o->max_turns = 3;
    o->max_segments = p && p->build.segments > 6 ? p->build.segments : 6;
    o->heat_gap_mm = 40.0;
    o->wall_gap_mm = 20.0;
    o->effort = 2;
    o->seed = 1;
}

PgFolder *pg_fold_begin(const PgProject *p, const PgFoldOpts *o)
{
    PgFolder *f = calloc(1, sizeof *f);
    if (!f)
        return NULL;
    f->c = calloc(1, sizeof *f->c);
    if (!f->c) {
        free(f);
        return NULL;
    }

    f->base = *p;
    f->base.route.n_bends = 0;
    f->o = *o;
    if (f->o.max_turns < 1) f->o.max_turns = 1;
    if (f->o.max_turns > MAX_TURNS) f->o.max_turns = MAX_TURNS;
    if (f->o.effort < 1) f->o.effort = 1;
    if (f->o.effort > 3) f->o.effort = 3;
    if (f->o.goal == PG_FOLD_FIT_BOX && !p->clear.enabled) {
        f->o.goal = PG_FOLD_COMPACT;
        f->box_fallback = true;
    }

    pg_design_compute(&f->base, &f->d);

    int lo = p->build.segments, hi = o->max_segments;
    if (hi < lo) hi = lo;
    if (hi > MAX_SEGMENTS) hi = MAX_SEGMENTS;
    for (int s = lo; s <= hi; s++)
        f->segs[f->n_segs++] = s;

    setup_segments(f, lo);
    f->base_pieces = 0;
    for (int i = 0; i < f->c->n_pieces; i++)
        if (f->c->piece[i].kind != PG_PIECE_DUCT)
            f->base_pieces++;

    f->runs = 6 * f->o.effort;
    f->iters = 50 * f->o.effort;
    f->refine_iters = 200 * f->o.effort;
    f->planned = (long)f->n_segs * (1 + (long)f->o.max_turns * f->runs * (f->iters + 1))
               + f->refine_iters + 1;

    f->rng = (uint64_t)f->o.seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull;
    if (!f->rng)
        f->rng = 1;
    f->best_cost = HUGE_VAL;
    f->k = 0;
    f->run = 0;
    f->iter = -1;
    return f;
}

static void describe(PgFolder *f, const Eval *e, const PgChain *exact)
{
    PgFoldResult *r = &f->res;
    char *s = r->summary;
    size_t cap = sizeof r->summary, n = 0;
    if (f->box_fallback)
        n += (size_t)snprintf(s + n, cap - n, "No clearance box is set, so it "
                              "was folded for compactness. ");

    if (!r->ok) {
        /* by how much, so a near miss can be told from a hopeless box: each
         * figure is the worst single shortfall, not a sum over pieces */
        n += (size_t)snprintf(s + n, cap - n, "No safe fold found. The closest "
                              "still has");
        const char *sep = " ";
        if (e->box_mm > 0.5 || exact->n_clash_box) {
            n += (size_t)snprintf(s + n, cap - n, "%spipe up to %.0f mm past the "
                                  "box's wall gap", sep, e->worst_box);
            sep = "; ";
        }
        if (e->engine_mm > 0.5 || exact->n_clash_engine) {
            n += (size_t)snprintf(s + n, cap - n, "%spipe up to %.0f mm inside the "
                                  "heat gap to the engine", sep, e->worst_engine);
            sep = "; ";
        }
        if (e->self_mm > 0.5 || exact->n_clash_self) {
            n += (size_t)snprintf(s + n, cap - n, "%spipe up to %.0f mm inside the "
                                  "heat gap to itself", sep, e->worst_self);
            sep = "; ";
        }
        if (e->angle_deg > 0.01 || e->radius_d > 0.001 || exact->n_sharp) {
            n += (size_t)snprintf(s + n, cap - n, "%sbends sharper than the rules",
                                  sep);
            sep = "; ";
        }
        if (e->mitre_mm > 0.5 || exact->n_bad_mitre)
            n += (size_t)snprintf(s + n, cap - n, "%spieces too short for their "
                                  "mitres", sep);
        snprintf(s + n, cap - n, ". Try a larger box, smaller gaps, more turns or "
                 "segments, or Thorough.");
        return;
    }

    if (r->bent_joints == 0) {
        snprintf(s + n, cap - n, "No fold needed: the straight chamber already "
                 "meets every rule%s.",
                 f->o.goal == PG_FOLD_FIT_BOX ? " and fits the box" : "");
        return;
    }

    char tight[48];
    if (isfinite(r->tightest_d))
        snprintf(tight, sizeof tight, "%.1f diameters", r->tightest_d);
    else
        snprintf(tight, sizeof tight, "no consecutive bends");
    snprintf(s + n, cap - n,
             "Folded into %d turn%s over %d joints (%.0f deg of bending, %d "
             "segment%s per section). Sharpest joint %.0f%% of its limit; "
             "tightest run %s; smallest gap %.0f mm. Chamber envelope %.0f x "
             "%.0f x %.0f mm.",
             r->turns, r->turns == 1 ? "" : "s", r->bent_joints,
             r->total_turn_deg, r->segments, r->segments == 1 ? "" : "s",
             100.0 * r->sharpest, tight, r->min_gap_mm,
             r->envelope[0], r->envelope[1], r->envelope[2]);
}

static void finish(PgFolder *f)
{
    PgFoldResult *r = &f->res;
    memset(r, 0, sizeof *r);

    setup_segments(f, f->best_seg > 0 ? f->best_seg : f->segs[0]);
    Eval e;
    evaluate(f, &f->best, &e);
    r->route = f->work.route;
    r->route.seam_deg = f->base.route.seam_deg;
    r->segments = f->work.build.segments;
    r->turns = e.turns;
    r->bent_joints = e.bent;
    r->total_turn_deg = e.raw_turn;
    r->sharpest = e.sharpest;
    r->tightest_d = e.tightest;
    r->min_gap_mm = isfinite(e.min_gap) ? e.min_gap : 0.0;
    r->evaluations = f->evals;

    /* the full surface check, which the fast model must not have fooled */
    pg_chain_build(&f->work, &f->d, f->c);
    const PgChain *c = f->c;
    for (int a = 0; a < 3; a++)
        r->envelope[a] = c->bmax[a] - c->bmin[a];
    bool box_ok = f->o.goal != PG_FOLD_FIT_BOX || c->n_clash_box == 0;
    r->ok = e.pen <= 0.0 && box_ok && c->n_clash_engine == 0 &&
            c->n_clash_self == 0 && c->n_bad_mitre == 0 && c->n_sharp == 0;
    describe(f, &e, c);
    f->finished = true;
}

bool pg_fold_step(PgFolder *f, int evals)
{
    if (!f)
        return true;
    while (evals-- > 0 && !f->finished) {
        Eval e;
        if (f->refining) {
            if (f->iter >= f->refine_iters) {
                finish(f);
                break;
            }
            Cand n;
            mutate(f, &f->cur, &n);
            double cost = evaluate(f, &n, &e);
            double T = 8.0 * pow(0.05, (double)f->iter / f->refine_iters);
            if (cost < f->cur_cost || urand(f) < exp(-(cost - f->cur_cost) / T)) {
                f->cur = n;
                f->cur_cost = cost;
            }
            consider(f, &n, cost);
            f->iter++;
            continue;
        }

        if (f->k == 0) {
            Cand z;
            memset(&z, 0, sizeof z);
            consider(f, &z, evaluate(f, &z, &e));
            f->k = 1;
            f->run = 0;
            f->iter = -1;
            continue;
        }

        if (f->iter < 0) {
            init_candidate(f, &f->cur);
            f->cur_cost = evaluate(f, &f->cur, &e);
            consider(f, &f->cur, f->cur_cost);
            f->iter = 0;
            continue;
        }

        if (f->iter < f->iters) {
            Cand n;
            mutate(f, &f->cur, &n);
            double cost = evaluate(f, &n, &e);
            double T = 60.0 * pow(0.02, (double)f->iter / f->iters);
            if (cost < f->cur_cost || urand(f) < exp(-(cost - f->cur_cost) / T)) {
                f->cur = n;
                f->cur_cost = cost;
            }
            consider(f, &n, cost);
            f->iter++;
            continue;
        }

        /* this run is over: the next run, turn count or segment count */
        f->iter = -1;
        if (++f->run < f->runs)
            continue;
        f->run = 0;
        if (++f->k <= f->o.max_turns)
            continue;
        f->k = 0;
        if (++f->seg_i < f->n_segs) {
            setup_segments(f, f->segs[f->seg_i]);
            continue;
        }

        /* refine the best from where it was found */
        setup_segments(f, f->best_seg);
        f->refining = true;
        f->cur = f->best;
        f->cur_cost = f->best_cost;
        f->iter = 0;
    }
    return f->finished;
}

double pg_fold_progress(const PgFolder *f)
{
    if (!f || f->finished)
        return 1.0;
    double p = (double)f->evals / (double)f->planned;
    return p > 0.99 ? 0.99 : p;
}

const PgFoldResult *pg_fold_result(const PgFolder *f)
{
    return f ? &f->res : NULL;
}

void pg_fold_end(PgFolder *f)
{
    if (!f)
        return;
    free(f->c);
    free(f);
}

bool pg_autofold(const PgProject *p, const PgFoldOpts *o, PgFoldResult *out)
{
    PgFolder *f = pg_fold_begin(p, o);
    if (!f) {
        memset(out, 0, sizeof *out);
        snprintf(out->summary, sizeof out->summary, "Out of memory.");
        return false;
    }
    while (!pg_fold_step(f, 1000))
        ;
    *out = f->res;
    pg_fold_end(f);
    return out->ok;
}

void pg_fold_apply(PgProject *p, const PgFoldResult *r)
{
    double seam = p->route.seam_deg;
    p->route = r->route;
    p->route.seam_deg = seam;
    p->build.segments = r->segments;
}
