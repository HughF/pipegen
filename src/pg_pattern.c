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
#include "pg_pattern.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define DEG (M_PI / 180.0)
#define MAX_ARC_PIECE (M_PI / 4.0)   /* arcs split so bulges stay small */

const char *pg_part_kind_name(PgPartKind k)
{
    switch (k) {
    case PG_PART_CONE:   return "cone";
    case PG_PART_WRAP:   return "cylinder";
    case PG_PART_PILLOW: return "hydroform half";
    case PG_PART_TUBE:   return "tube";
    default:             return "?";
    }
}

static void warn(PgPartList *pl, const char *fmt, ...)
{
    if (pl->n_warn >= 8)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(pl->warn[pl->n_warn], PG_WARN_LEN, fmt, ap);
    va_end(ap);
    pl->n_warn++;
}

static PgPart *new_part(PgPartList *pl, PgPartKind kind, int piece, int qty)
{
    if (pl->n >= PG_MAX_PARTS)
        return NULL;
    PgPart *p = &pl->part[pl->n];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->piece = piece;
    p->qty = qty;
    snprintf(p->id, sizeof p->id, "P%02d", pl->n + 1);
    pl->n++;
    return p;
}

static void vert(PgPart *p, double x, double y, double bulge)
{
    if (p->n >= PG_PART_VERTS)
        return;
    p->v[p->n].x = x;
    p->v[p->n].y = y;
    p->v[p->n].bulge = bulge;
    p->n++;
}

static void mark(PgPart *p, double x0, double y0, double x1, double y1)
{
    if (p->n_marks >= PG_PART_MARKS)
        return;
    PgLine *m = &p->marks[p->n_marks++];
    m->x0 = x0; m->y0 = y0; m->x1 = x1; m->y1 = y1;
}

/* Append an arc about the origin from angle a0 to a1 at radius r, as bulged
 * vertices. The end vertex is not emitted: the caller's next vertex is it. */
static void arc_verts(PgPart *p, double r, double a0, double a1)
{
    double sweep = a1 - a0;
    int k = (int)ceil(fabs(sweep) / MAX_ARC_PIECE);
    if (k < 1)
        k = 1;
    double step = sweep / k;
    double b = pg_bulge_for_sweep(step);
    for (int i = 0; i < k; i++) {
        double a = a0 + step * i;
        vert(p, r * cos(a), r * sin(a), b);
    }
}

static void finish(PgPart *p)
{
    p->box = pg_path_box(p->v, p->n, true);
    p->area_mm2 = fabs(pg_path_area(p->v, p->n));
}

/* ------------------------------------------------------------------ */
/* Square-ended pieces: exact arcs                                     */
/* ------------------------------------------------------------------ */

/*
 * A frustum of mean diameters ds < dl and axial length h unrolls to a sector
 * of an annulus. Its slant height s is the annulus width; the outer radius is
 * the apex-to-large-rim slant distance, R2 = s * dl / (dl - ds); and the
 * sector's included angle makes the outer arc exactly one circumference,
 * phi = pi * dl / R2. The sector opens upwards, symmetric about the y axis,
 * apex at the origin.
 *
 * A seam allowance widens the right-hand radial edge by a parallel offset, so
 * the extra is the same width all along the seam.
 */
static void make_cone(PgPart *p, double ds, double dl, double h,
                      double allow, bool marks)
{
    if (ds > dl) { double t = ds; ds = dl; dl = t; }

    double half = (dl - ds) / 2.0;
    double s = hypot(h, half);
    double R2 = s * dl / (dl - ds);
    double R1 = R2 - s;
    double phi = M_PI * dl / R2;

    p->d_small = ds;
    p->d_large = dl;
    p->axial_len = h;
    p->slant = s;
    p->r_inner = R1;
    p->r_outer = R2;
    p->sweep_deg = phi / DEG;

    double aR = M_PI / 2.0 - phi / 2.0;
    double aL = M_PI / 2.0 + phi / 2.0;
    double aRo = aR - (allow > 0.0 && allow < R2 ? asin(allow / R2) : 0.0);
    double aRi = aR - (allow > 0.0 && allow < R1 ? asin(allow / R1) : 0.0);

    arc_verts(p, R2, aRo, aL);
    vert(p, R2 * cos(aL), R2 * sin(aL), 0.0);
    if (R1 > 1e-6) {
        arc_verts(p, R1, aL, aRi);
        vert(p, R1 * cos(aRi), R1 * sin(aRi), 0.0);
    } else {
        vert(p, 0.0, 0.0, 0.0);
    }

    if (marks) {
        /* Quarter-circumference ticks on both rims: they line up when the
         * cone is rolled, and between neighbouring cones when they are
         * tacked together. */
        double m = fmin(6.0, s / 8.0);
        for (int q = 1; q <= 3; q++) {
            double a = aR + phi * q / 4.0;
            double c = cos(a), sn = sin(a);
            mark(p, R2 * c, R2 * sn, (R2 - m) * c, (R2 - m) * sn);
            if (R1 > m)
                mark(p, R1 * c, R1 * sn, (R1 + m) * c, (R1 + m) * sn);
        }
    }

    p->label_x = 0.0;
    p->label_y = (R1 + R2) / 2.0;
    finish(p);
}

/* A cylinder of mean diameter dm and length h unrolls to a rectangle one
 * circumference wide. */
static void make_wrap(PgPart *p, double dm, double h, double allow, bool marks)
{
    double w = M_PI * dm;

    p->d_small = p->d_large = dm;
    p->axial_len = h;
    p->slant = h;
    p->flat_w = w + allow;
    p->flat_l = h;

    vert(p, 0.0, 0.0, 0.0);
    vert(p, w + allow, 0.0, 0.0);
    vert(p, w + allow, h, 0.0);
    vert(p, 0.0, h, 0.0);

    if (marks) {
        double m = fmin(6.0, h / 8.0);
        for (int q = 1; q <= 3; q++) {
            double x = w * q / 4.0;
            mark(p, x, 0.0, x, m);
            mark(p, x, h, x, h - m);
        }
    }

    p->label_x = (w + allow) / 2.0;
    p->label_y = h / 2.0;
    finish(p);
}

/* ------------------------------------------------------------------ */
/* Mitred pieces: developed edges                                      */
/* ------------------------------------------------------------------ */

/*
 * Development maps a point on the cone at azimuth u from the seam and local
 * height z to the flat. For a cylinder that is (u r, z). For a cone it is
 * polar about the apex: the distance from the apex along the surface,
 * rho = r(z) / sin(alpha), at the angle u sin(alpha), where alpha is the
 * cone's half-angle. Both are isometries, so a curve on the surface keeps
 * its length on the flat — which is what makes the edges meet when rolled.
 */
typedef struct {
    const PgPiece *pc;
    double rA, rB;
    double seam;          /* radians                                     */
    bool   cyl;
    double sina, base;    /* cone: sin(half-angle), sector start angle   */
} Dev;

static void dev_point(const Dev *dv, double u, double z, double *x, double *y)
{
    if (dv->cyl) {
        *x = u * dv->rA;
        *y = z;
        return;
    }
    double k = (dv->rB - dv->rA) / dv->pc->len;
    double r = dv->rA + k * z;
    double rho = r / dv->sina;
    double a = dv->base + u * dv->sina;
    *x = rho * cos(a);
    *y = rho * sin(a);
}

static double dev_end_z(const Dev *dv, double u, int end)
{
    return pg_piece_end_z(dv->pc, dv->rA, dv->rB, dv->seam + u, end);
}

static void make_mitred(PgPart *p, const PgPiece *pc, double rA, double rB,
                        double seam_deg, double allow, bool marks)
{
    Dev dv = { pc, rA, rB, seam_deg * DEG, fabs(rB - rA) < 1e-6, 1.0, 0.0 };
    double k = (rB - rA) / pc->len;
    double g = sqrt(1.0 + k * k);
    if (!dv.cyl) {
        dv.sina = fabs(k) / g;
        dv.base = M_PI / 2.0 - M_PI * dv.sina;     /* centred on +y */
    }

    p->d_small = 2.0 * fmin(rA, rB);
    p->d_large = 2.0 * fmax(rA, rB);
    p->axial_len = pc->len;
    p->slant = pc->len * g;
    p->mitred = true;
    if (dv.cyl) {
        p->flat_w = 2.0 * M_PI * rA + allow;
        p->flat_l = pc->len;
    } else {
        p->r_inner = fmin(rA, rB) / dv.sina;
        p->r_outer = fmax(rA, rB) / dv.sina;
        p->sweep_deg = 2.0 * M_PI * dv.sina / DEG;
    }

    const int N = PG_MITRE_SAMPLES;
    double umax0 = 2.0 * M_PI + allow / rA;
    double umax1 = 2.0 * M_PI + allow / rB;

    /* start edge forwards, finish edge backwards */
    for (int i = 0; i <= N; i++) {
        double u = umax0 * i / N, x, y;
        dev_point(&dv, u, dev_end_z(&dv, u, 0), &x, &y);
        vert(p, x, y, 0.0);
    }
    for (int i = N; i >= 0; i--) {
        double u = umax1 * i / N, x, y;
        dev_point(&dv, u, dev_end_z(&dv, u, 1), &x, &y);
        vert(p, x, y, 0.0);
    }

    if (marks) {
        double mlen = fmin(6.0, p->slant / 8.0);
        for (int end = 0; end < 2; end++) {
            double r = end ? rB : rA;
            double dir = end ? -1.0 : 1.0;          /* into the part */
            /* quarter ticks from the seam */
            for (int q = 1; q <= 3; q++) {
                double u = M_PI / 2.0 * q;
                double z = dev_end_z(&dv, u, end), x0, y0, x1, y1;
                dev_point(&dv, u, z, &x0, &y0);
                dev_point(&dv, u, z + dir * mlen / g, &x1, &y1);
                mark(p, x0, y0, x1, y1);
            }
            /* the inside of the bend, as a double tick */
            double cut = end ? pc->cut1_deg : pc->cut0_deg;
            if (cut > 1e-6) {
                double roll = (end ? pc->bend1_roll : pc->bend0_roll) * DEG;
                double u0 = fmod(roll - dv.seam + 4.0 * M_PI, 2.0 * M_PI);
                for (int t = -1; t <= 1; t += 2) {
                    double u = u0 + t * 2.0 / r;
                    double z = dev_end_z(&dv, u, end), x0, y0, x1, y1;
                    dev_point(&dv, u, z, &x0, &y0);
                    dev_point(&dv, u, z + dir * 2.0 * mlen / g, &x1, &y1);
                    mark(p, x0, y0, x1, y1);
                }
            }
        }
    }

    double zm = (dev_end_z(&dv, M_PI, 0) + dev_end_z(&dv, M_PI, 1)) / 2.0;
    dev_point(&dv, M_PI, zm, &p->label_x, &p->label_y);
    finish(p);
}

static void make_tube(PgPart *p, const PgPiece *pc, double t)
{
    p->tube_id = pc->d0;
    p->tube_od = pc->d0 + 2.0 * t;
    p->d_small = p->d_large = pc->d0 + t;
    p->axial_len = pc->len;
    /* the longest side of a tube mitred at either end */
    p->tube_len = pc->len + p->tube_od / 2.0 *
                  (tan(pc->cut0_deg * DEG) + tan(pc->cut1_deg * DEG));
    p->mitred = pc->cut0_deg > 1e-6 || pc->cut1_deg > 1e-6;
}

/* ------------------------------------------------------------------ */
/* Rolled and welded                                                   */
/* ------------------------------------------------------------------ */

static void build_rolled(const PgProject *pr, PgChain *c, PgPartList *pl)
{
    const PgBuild *b = &pr->build;
    double t = b->thickness_mm;
    int cyl = pr->engine.cylinders;

    for (int i = 0; i < c->n_pieces; i++) {
        PgPiece *pc = &c->piece[i];
        if (pc->kind == PG_PIECE_DUCT)
            continue;

        PgPartKind kind = pc->kind == PG_PIECE_TUBE ? PG_PART_TUBE
                        : fabs(pc->d1 - pc->d0) < 0.05 ? PG_PART_WRAP
                        : PG_PART_CONE;
        PgPart *p = new_part(pl, kind, i, cyl);
        if (!p) {
            warn(pl, "More than %d parts; the rest were dropped.", PG_MAX_PARTS);
            return;
        }
        pc->part = pl->n - 1;
        snprintf(p->name, sizeof p->name, "%s", pc->name);
        p->cut0_deg = pc->cut0_deg;
        p->cut1_deg = pc->cut1_deg;

        if (kind == PG_PART_TUBE) {
            make_tube(p, pc, t);
            if (fabs(pc->d1 - pc->d0) > 0.05)
                warn(pl, "%s tapers %.1f to %.1f mm but is cut from tube. Use "
                         "tube of the larger size, or roll it from sheet.",
                     pc->name, pc->d0, pc->d1);
            continue;
        }

        bool square = pc->cut0_deg < 1e-6 && pc->cut1_deg < 1e-6;
        double dmA = pc->d0 + t, dmB = pc->d1 + t;
        if (kind == PG_PART_WRAP)
            dmB = dmA;

        if (!square)
            make_mitred(p, pc, dmA / 2.0, dmB / 2.0, pr->route.seam_deg,
                        b->seam_allow_mm, b->etch_marks);
        else if (kind == PG_PART_WRAP)
            make_wrap(p, dmA, pc->len, b->seam_allow_mm, b->etch_marks);
        else
            make_cone(p, dmA, dmB, pc->len, b->seam_allow_mm, b->etch_marks);
    }
}

/* ------------------------------------------------------------------ */
/* Hydroformed                                                         */
/* ------------------------------------------------------------------ */

/*
 * A flat half of width W inflates to a half-round of circumference W, so
 * W = pi * D / 2 at every station.
 *
 * The edge seam is a weld bead: stiff, and it hardly stretches. With seam
 * compensation on, every stretch of flat is sized so its edge is as long as
 * the inflated wall it becomes — shorter than the section on a cone, because
 * the flat edge steps out by pi/4 of the diameter change and the inflated wall
 * by only 1/2 of it. A bend follows the same rule: across a joint the flat
 * edge sits pi/2 times further from the centreline than the inflated seam
 * does, so the flat outline turns through less than the finished bend.
 * First-order models, both — inflate a test piece before cutting a set.
 */
typedef struct {
    double s;          /* flat distance along the centreline         */
    double x, y;       /* centreline point                           */
    double nrm;        /* direction of the station line, radians     */
    double scale;      /* 1 / cos(half the turn) at a mitred station */
    double hw;         /* half-width with margin                     */
    bool   joint;
} Station;

static void station_edge(const Station *st, int side, double *x, double *y)
{
    double o = st->hw * st->scale * side;
    *x = st->x + cos(st->nrm) * o;
    *y = st->y + sin(st->nrm) * o;
}

static double flat_turn(const PgProject *pr, const PgJoint *jt)
{
    if (jt->bend_deg < 1e-9)
        return 0.0;
    double b = jt->bend_deg * DEG;
    double sign = fabs(remainder(jt->roll_deg - (pr->route.seam_deg + 90.0), 360.0))
                < 90.0 ? 1.0 : -1.0;
    double tau = pr->build.hydro_seam_comp
               ? 2.0 * atan(2.0 / M_PI * tan(b / 2.0)) : b;
    return sign * tau;
}

static void build_hydro(const PgProject *pr, PgChain *c, PgPartList *pl)
{
    const PgBuild *b = &pr->build;
    double t = b->thickness_mm;
    int cyl = pr->engine.cylinders;

    int first = -1, last = -1;
    for (int i = 0; i < c->n_pieces; i++) {
        PgPiece *pc = &c->piece[i];
        if (pc->kind == PG_PIECE_SHEET) {
            if (first < 0) first = i;
            last = i;
        } else if (pc->kind == PG_PIECE_TUBE) {
            PgPart *p = new_part(pl, PG_PART_TUBE, i, cyl);
            if (!p)
                return;
            pc->part = pl->n - 1;
            snprintf(p->name, sizeof p->name, "%s", pc->name);
            p->cut0_deg = pc->cut0_deg;
            p->cut1_deg = pc->cut1_deg;
            make_tube(p, pc, t);
        }
    }
    if (first < 0)
        return;

    /* stations at every piece boundary of the sheet run */
    Station st[PG_MAX_PIECES + 1];
    int n = 0;
    double x = 0.0, y = 0.0, head = 0.0, s = 0.0;
    bool too_tight = false;

    for (int i = first; i <= last + 1; i++) {
        const PgPiece *prev = i > first ? &c->piece[i - 1] : NULL;
        const PgPiece *next = i <= last ? &c->piece[i] : NULL;
        double dm = (next ? next->d0 : prev->d1) + t;

        /* the turn at this station: at the ends, half the mitre of the joint
         * to the tube or duct beyond */
        double turn = 0.0;
        int j = next ? i - 1 : i - 1;             /* joint before piece i */
        if (j >= 0 && j < c->n_joints)
            turn = flat_turn(pr, &c->joint[j]);
        double lean = (prev && next) ? turn / 2.0 : turn / 2.0;

        Station *sp = &st[n++];
        sp->s = s;
        sp->x = x;
        sp->y = y;
        sp->nrm = head + M_PI / 2.0 + (prev && next ? turn / 2.0
                                       : next ? lean : lean);
        sp->scale = 1.0 / cos(turn / 2.0);
        sp->hw = M_PI * dm / 4.0 + b->hydro_margin_mm;
        sp->joint = true;

        if (prev && next) {
            head += turn;
            if (sp->hw * fabs(tan(turn / 2.0)) > fmin(prev->len, next->len))
                too_tight = true;
        }

        if (next) {
            double dA = next->d0 + t, dB = next->d1 + t;
            double run = next->len;
            if (b->hydro_seam_comp) {
                double wall = hypot(next->len, (dB - dA) / 2.0);
                double step = M_PI * (dB - dA) / 4.0;
                if (wall > fabs(step))
                    run = sqrt(wall * wall - step * step);
            }
            x += run * cos(head);
            y += run * sin(head);
            s += run;
        }
    }
    if (too_tight)
        warn(pl, "A bend is too tight for the hydroformed halves: the inner "
                 "edge folds over itself. Bend less or use longer segments.");

    /* split into pieces that fit the sheet */
    double total = st[n - 1].s;
    double longest = fmax(b->sheet_w_mm, b->sheet_h_mm) - 2.0 * b->part_gap_mm;
    int pieces = (longest > 0.0) ? (int)ceil(total / longest) : 1;
    if (pieces < 1)
        pieces = 1;

    for (int pc_i = first; pc_i <= last; pc_i++)
        c->piece[pc_i].part = pl->n + (int)fmin(pieces - 1,
            floor((c->piece[pc_i].x0 - c->piece[first].x0 + c->piece[pc_i].len / 2.0)
                  / (c->piece[last].x0 + c->piece[last].len - c->piece[first].x0)
                  * pieces));

    for (int jp = 0; jp < pieces; jp++) {
        double a = total * jp / pieces, e = total * (jp + 1) / pieces;

        PgPart *p = new_part(pl, PG_PART_PILLOW, -1, 2 * cyl);
        if (!p)
            return;
        if (pieces == 1)
            snprintf(p->name, sizeof p->name, "Chamber half");
        else
            snprintf(p->name, sizeof p->name, "Chamber half, piece %d of %d",
                     jp + 1, pieces);

        /* stations in this piece, with cut stations interpolated at a and e */
        Station loc[PG_MAX_PIECES + 3];
        int m = 0;
        for (int k = 0; k < 2; k++) {
            double sc = k ? e : a;
            if (k == 1) {
                for (int i = 0; i < n; i++)
                    if (st[i].s > a + 1e-6 && st[i].s < e - 1e-6)
                        loc[m++] = st[i];
            }
            /* the exact station, or a square cut inside a straight run */
            int hit = -1;
            for (int i = 0; i < n; i++)
                if (fabs(st[i].s - sc) <= 1e-6)
                    hit = i;
            if (hit >= 0) {
                loc[m++] = st[hit];
            } else {
                int i = 1;
                while (i < n - 1 && st[i].s < sc)
                    i++;
                const Station *s0 = &st[i - 1], *s1 = &st[i];
                double u = (sc - s0->s) / (s1->s - s0->s);
                Station q = *s0;
                q.s = sc;
                q.x = s0->x + (s1->x - s0->x) * u;
                q.y = s0->y + (s1->y - s0->y) * u;
                q.nrm = atan2(s1->y - s0->y, s1->x - s0->x) + M_PI / 2.0;
                q.scale = 1.0;
                q.hw = s0->hw + (s1->hw - s0->hw) * u;
                q.joint = false;
                loc[m++] = q;
            }
        }

        double ox = loc[0].x, oy = loc[0].y;
        for (int i = 0; i < m; i++) {
            double ex, ey;
            station_edge(&loc[i], -1, &ex, &ey);
            vert(p, ex - ox, ey - oy, 0.0);
        }
        for (int i = m - 1; i >= 0; i--) {
            double ex, ey;
            station_edge(&loc[i], 1, &ex, &ey);
            vert(p, ex - ox, ey - oy, 0.0);
        }

        double wmax = 0.0;
        p->n_st = m;
        for (int i = 0; i < m; i++) {
            p->st_x[i] = loc[i].s - a;
            p->st_hw[i] = loc[i].hw;
            if (loc[i].hw > wmax)
                wmax = loc[i].hw;
        }
        p->flat_w = 2.0 * wmax;
        p->flat_l = e - a;
        p->axial_len = e - a;

        if (b->etch_marks) {
            const double tick = 8.0;
            for (int i = 0; i < m; i++) {
                if (!loc[i].joint || i == 0 || i == m - 1)
                    continue;
                for (int side = -1; side <= 1; side += 2) {
                    double ex, ey;
                    station_edge(&loc[i], side, &ex, &ey);
                    mark(p, ex - ox, ey - oy,
                         ex - ox - side * cos(loc[i].nrm) * tick,
                         ey - oy - side * sin(loc[i].nrm) * tick);
                }
            }
            /* centreline at both ends, to line the two halves up */
            for (int k = 0; k < 2; k++) {
                const Station *q = &loc[k ? m - 1 : 0];
                double hx = cos(q->nrm - M_PI / 2.0), hy = sin(q->nrm - M_PI / 2.0);
                double sgn = k ? -1.0 : 1.0;
                mark(p, q->x - ox, q->y - oy,
                     q->x - ox + sgn * hx * tick, q->y - oy + sgn * hy * tick);
            }
        }

        const Station *mid = &loc[m / 2];
        p->label_x = mid->x - ox;
        p->label_y = mid->y - oy;
        finish(p);
    }
}

void pg_parts_build(const PgProject *pr, const PgDesign *d, PgChain *c,
                    PgPartList *out)
{
    (void)d;
    memset(out, 0, sizeof *out);
    for (int i = 0; i < c->n_pieces; i++)
        c->piece[i].part = -1;

    if (pr->build.method == PG_MFG_HYDRO)
        build_hydro(pr, c, out);
    else
        build_rolled(pr, c, out);

    double t_m = pr->build.thickness_mm / 1000.0;
    double rho = pg_material_density(pr->build.material);
    for (int i = 0; i < out->n; i++) {
        const PgPart *p = &out->part[i];
        if (p->kind == PG_PART_TUBE)
            continue;
        out->sheet_area_mm2 += p->area_mm2 * p->qty;
    }
    out->mass_kg = out->sheet_area_mm2 / 1e6 * t_m * rho;
}

/* ------------------------------------------------------------------ */
/* Layout                                                              */
/* ------------------------------------------------------------------ */

void pg_place_point(const PgPlace *pl, double x, double y, double *ox, double *oy)
{
    pg_xform(x, y, pl->rot, pl->x, pl->y, ox, oy);
}

typedef struct {
    int    part, copy;
    double rot, w, h, minx, miny;
    bool   fits;
} Item;

static PgBox rotated_box(const PgPart *p, double rot)
{
    PgVert tmp[PG_PART_VERTS];
    for (int i = 0; i < p->n; i++) {
        pg_xform(p->v[i].x, p->v[i].y, rot, 0.0, 0.0, &tmp[i].x, &tmp[i].y);
        tmp[i].bulge = p->v[i].bulge;
    }
    return pg_path_box(tmp, p->n, true);
}

static int item_cmp(const void *a, const void *b)
{
    const Item *x = a, *y = b;
    if (fabs(x->h - y->h) > 1e-6)
        return x->h > y->h ? -1 : 1;
    if (x->part != y->part)
        return x->part - y->part;
    return x->copy - y->copy;
}

typedef struct { double y, h, used_w; } Shelf;
typedef struct { double used_h; int n_shelf; Shelf shelf[64]; } SheetFill;

void pg_layout_build(const PgPartList *parts, const PgBuild *b, PgLayout *out)
{
    memset(out, 0, sizeof *out);
    out->sheet_w = b->sheet_w_mm;
    out->sheet_h = b->sheet_h_mm;
    out->gap = b->part_gap_mm;

    double gap = b->part_gap_mm;
    double W = b->sheet_w_mm - 2.0 * gap;
    double H = b->sheet_h_mm - 2.0 * gap;

    /* best orientation per part: the flattest one that fits the sheet; a
     * two-degree search is ample for rows of simple shapes */
    Item best[PG_MAX_PARTS];
    for (int i = 0; i < parts->n; i++) {
        const PgPart *p = &parts->part[i];
        Item it = { .part = i, .fits = false };
        double best_area = HUGE_VAL;

        if (p->kind == PG_PART_TUBE || p->n == 0) {
            best[i] = it;
            continue;
        }
        for (int deg = 0; deg < 180; deg += 2) {
            double rot = deg * DEG;
            PgBox bx = rotated_box(p, rot);
            double w = bx.maxx - bx.minx, h = bx.maxy - bx.miny;
            bool fits = (w <= W && h <= H);

            if (fits && (!it.fits || h < it.h - 1e-6 ||
                         (fabs(h - it.h) <= 1e-6 && w < it.w))) {
                it.fits = true;
                it.rot = rot; it.w = w; it.h = h;
                it.minx = bx.minx; it.miny = bx.miny;
            } else if (!it.fits && w * h < best_area) {
                best_area = w * h;
                it.rot = rot; it.w = w; it.h = h;
                it.minx = bx.minx; it.miny = bx.miny;
            }
        }
        best[i] = it;
    }

    Item items[PG_MAX_PLACE];
    int n = 0;
    for (int i = 0; i < parts->n; i++) {
        if (parts->part[i].kind == PG_PART_TUBE)
            continue;
        for (int c = 1; c <= parts->part[i].qty; c++) {
            if (n >= PG_MAX_PLACE) {
                out->n_unplaced++;
                continue;
            }
            items[n] = best[i];
            items[n].copy = c;
            n++;
        }
    }
    qsort(items, (size_t)n, sizeof items[0], item_cmp);

    static SheetFill fill[PG_MAX_SHEETS];
    memset(fill, 0, sizeof fill);

    for (int k = 0; k < n; k++) {
        const Item *it = &items[k];
        int sheet = -1;
        double sx = 0.0, sy = 0.0;

        if (it->fits) {
            for (int s = 0; s < out->n_sheets && sheet < 0; s++) {
                if (out->oversize[s])
                    continue;
                SheetFill *f = &fill[s];
                for (int r = 0; r < f->n_shelf; r++) {
                    Shelf *sh = &f->shelf[r];
                    double need = sh->used_w > 0.0 ? sh->used_w + gap + it->w
                                                   : it->w;
                    if (it->h <= sh->h && need <= W) {
                        sx = sh->used_w > 0.0 ? sh->used_w + gap : 0.0;
                        sy = sh->y;
                        sh->used_w = need;
                        sheet = s;
                        break;
                    }
                }
                if (sheet < 0 && f->n_shelf < 64) {
                    double y = f->n_shelf ? f->used_h + gap : 0.0;
                    if (y + it->h <= H) {
                        Shelf *sh = &f->shelf[f->n_shelf++];
                        sh->y = y;
                        sh->h = it->h;
                        sh->used_w = it->w;
                        f->used_h = y + it->h;
                        sx = 0.0;
                        sy = y;
                        sheet = s;
                    }
                }
            }
        }

        if (sheet < 0) {
            if (out->n_sheets >= PG_MAX_SHEETS) {
                out->n_unplaced++;
                continue;
            }
            sheet = out->n_sheets++;
            SheetFill *f = &fill[sheet];
            f->n_shelf = 1;
            f->shelf[0].y = 0.0;
            f->shelf[0].h = it->h;
            f->shelf[0].used_w = it->w;
            f->used_h = it->h;
            sx = sy = 0.0;
            if (!it->fits) {
                out->oversize[sheet] = true;
                out->n_oversize++;
            }
        }

        PgPlace *pl = &out->place[out->n++];
        pl->part = it->part;
        pl->copy = it->copy;
        pl->sheet = sheet;
        pl->rot = it->rot;
        pl->x = gap + sx - it->minx;
        pl->y = gap + sy - it->miny;
    }
}
