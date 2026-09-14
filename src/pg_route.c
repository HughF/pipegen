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
#include "pg_route.h"
#include "pg_vec.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define DEG (M_PI / 180.0)

static void warn(PgChain *c, const char *fmt, ...)
{
    if (c->n_warn >= 8)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->warn[c->n_warn], PG_WARN_LEN, fmt, ap);
    va_end(ap);
    c->n_warn++;
}

const char *pg_roll_name(double roll_deg)
{
    double r = fmod(roll_deg, 360.0);
    if (r < 0.0) r += 360.0;
    if (r < 22.5 || r >= 337.5) return "down";
    if (r < 67.5)  return "down, towards -Z";
    if (r < 112.5) return "towards -Z";
    if (r < 157.5) return "up, towards -Z";
    if (r < 202.5) return "up";
    if (r < 247.5) return "up, towards +Z";
    if (r < 292.5) return "towards +Z";
    return "down, towards +Z";
}

void pg_engine_stub(const PgEngine *e, PgStub *s)
{
    double b = e->bore_mm, st = e->stroke_mm;
    memset(s, 0, sizeof *s);
    s->barrel_r  = b / 2.0 + 0.38 * b;
    s->barrel_y0 = -0.75 * st;
    s->barrel_y1 = 1.05 * st;
    s->head_r    = s->barrel_r * 0.95;
    s->head_y1   = s->barrel_y1 + 0.35 * b;
    v3_set(s->case_min, -0.95 * b, s->barrel_y0 - 1.5 * st, -1.1 * b);
    v3_set(s->case_max,  0.95 * b, s->barrel_y0,             1.1 * b);
    v3_set(s->port, b / 2.0, 0.0, 0.0);
}

double pg_piece_end_z(const PgPiece *pc, double rA, double rB, double psi, int end)
{
    const double *n = end ? pc->n1 : pc->n0;
    double zc = end ? pc->len : 0.0;
    double q = n[0] * cos(psi) + n[1] * sin(psi);
    double k = pc->len > 0.0 ? (rB - rA) / pc->len : 0.0;
    /* on the plane: q r(z) + nz (z - zc) = 0, with r(z) = rA + k z */
    return (n[2] * zc - q * rA) / (k * q + n[2]);
}

void pg_piece_point(const PgPiece *pc, double rA, double rB, double psi,
                    double z, double out[3])
{
    double k = pc->len > 0.0 ? (rB - rA) / pc->len : 0.0;
    double r = rA + k * z;
    double c = cos(psi) * r, s = sin(psi) * r;
    for (int i = 0; i < 3; i++)
        out[i] = pc->p0[i] + pc->axis[i] * z + pc->ref[i] * c + pc->side[i] * s;
}

int pg_chain_joint_near(const PgChain *c, double x)
{
    int best = -1;
    double bd = HUGE_VAL;
    for (int i = 0; i < c->n_joints; i++) {
        double d = fabs(c->joint[i].x - x);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* ------------------------------------------------------------------ */

static PgPiece *add_piece(PgChain *c, PgPieceKind kind, int section,
                          const char *name, double x0, double len,
                          const PgDesign *d)
{
    if (c->n_pieces >= PG_MAX_PIECES) {
        if (c->n_pieces == PG_MAX_PIECES)
            warn(c, "More than %d pieces; use fewer segments.", PG_MAX_PIECES);
        return NULL;
    }
    PgPiece *p = &c->piece[c->n_pieces++];
    memset(p, 0, sizeof *p);
    p->kind = kind;
    p->section = section;
    p->sec_kind = d->sec[section].kind;
    p->stage = d->sec[section].stage;
    p->seg = p->nseg = 1;
    snprintf(p->name, sizeof p->name, "%s", name);
    p->x0 = x0;
    p->len = len;
    int s;
    p->d0 = pg_design_diameter_at(d, x0, &s);
    p->d1 = pg_design_diameter_at(d, x0 + len, &s);
    /* the end of the last section has no following section to find */
    if (s < 0 && section >= 0)
        p->d1 = d->sec[section].d1;
    p->part = -1;
    v3_set(p->n0, 0, 0, 1);
    v3_set(p->n1, 0, 0, 1);
    return p;
}

static void split(PgChain *c, PgPieceKind kind, int section, const char *base,
                  double x0, double len, int nseg, const PgDesign *d)
{
    for (int k = 0; k < nseg; k++) {
        char name[48];
        if (nseg == 1)
            snprintf(name, sizeof name, "%s", base);
        else
            snprintf(name, sizeof name, "%s (%d of %d)", base, k + 1, nseg);
        PgPiece *p = add_piece(c, kind, section, name, x0 + len * k / nseg,
                               len / nseg, d);
        if (!p)
            return;
        p->seg = k + 1;
        p->nseg = nseg;
    }
}

static void build_pieces(const PgProject *pr, const PgDesign *d, PgChain *c)
{
    const PgBuild *b = &pr->build;

    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];

        if (s->kind == PG_SEC_HEADER) {
            double duct = pr->engine.duct_len_mm;
            if (duct > s->len * 0.8) {
                warn(c, "The exhaust duct (%.0f mm) is most of the header "
                        "(%.0f mm); it was shortened to %.0f mm. The chamber "
                        "needs a shorter duct or a lower design speed.",
                     duct, s->len, s->len * 0.8);
                duct = s->len * 0.8;
            }
            if (duct > 0.5)
                add_piece(c, PG_PIECE_DUCT, i, "Exhaust duct (casting)",
                          s->x0, duct, d);
            if (b->header_tube)
                split(c, PG_PIECE_TUBE, i, "Header tube", s->x0 + duct,
                      s->len - duct, 1, d);
            else
                split(c, PG_PIECE_SHEET, i, s->name, s->x0 + duct,
                      s->len - duct, b->segments, d);
        } else if (s->kind == PG_SEC_STINGER && b->stinger_tube) {
            split(c, PG_PIECE_TUBE, i, "Stinger tube", s->x0, s->len, 1, d);
        } else {
            split(c, PG_PIECE_SHEET, i, s->name, s->x0, s->len, b->segments, d);
        }
    }

    c->n_joints = c->n_pieces > 0 ? c->n_pieces - 1 : 0;
    for (int j = 0; j < c->n_joints; j++) {
        PgJoint *jt = &c->joint[j];
        jt->before = j;
        jt->after = j + 1;
        jt->x = c->piece[j + 1].x0;
        jt->bend_index = -1;
    }
}

/* Put each bend on its nearest joint. Hydroformed chambers can only bend in
 * the plane of their seams, so their rolls are turned onto that plane. */
static void assign_bends(const PgProject *pr, PgChain *c)
{
    bool snapped = false;
    const PgRoute *r = &pr->route;

    for (int i = 0; i < r->n_bends; i++) {
        const PgBend *bd = &r->bends[i];
        int j = pg_chain_joint_near(c, bd->at_mm);
        if (j < 0)
            continue;
        PgJoint *jt = &c->joint[j];
        if (jt->bend_index >= 0)
            warn(c, "Two bends fall on the joint at %.0f mm; the later one "
                    "was used.", jt->x);

        double roll = bd->roll_deg;
        if (pr->build.method == PG_MFG_HYDRO) {
            double a = fmod(pr->route.seam_deg + 90.0, 360.0);
            double b = fmod(pr->route.seam_deg + 270.0, 360.0);
            double da = fabs(remainder(roll - a, 360.0));
            double db = fabs(remainder(roll - b, 360.0));
            double want = da <= db ? a : b;
            if (fabs(remainder(roll - want, 360.0)) > 0.5)
                snapped = true;
            roll = want;
        }
        jt->bend_deg = bd->bend_deg;
        jt->roll_deg = roll;
        jt->bend_index = i;
    }
    if (snapped)
        warn(c, "A hydroformed chamber can only bend in the plane of its "
                "seams; bend directions were turned onto that plane.");
}

static void place_pieces(const PgProject *pr, PgChain *c)
{
    double pd = pr->engine.port_down_deg * DEG;
    double p[3], dir[3], ref[3], side[3];
    v3_copy(p, c->stub.port);
    v3_set(dir, cos(pd), -sin(pd), 0.0);
    /* straight down, less its component along the axis */
    double down[3] = { 0.0, -1.0, 0.0 };
    v3_add_scaled(ref, down, dir, -v3_dot(down, dir));
    v3_norm(ref);
    v3_cross(side, dir, ref);

    for (int i = 0; i < c->n_pieces; i++) {
        PgPiece *pc = &c->piece[i];
        v3_copy(pc->p0, p);
        v3_copy(pc->axis, dir);
        v3_copy(pc->ref, ref);
        v3_copy(pc->side, side);
        v3_add_scaled(pc->p1, p, dir, pc->len);
        v3_copy(p, pc->p1);

        if (i < c->n_joints) {
            PgJoint *jt = &c->joint[i];
            v3_copy(jt->pos, p);
            double b = jt->bend_deg * DEG, r = jt->roll_deg * DEG;
            if (b > 1e-9) {
                double w[3], axis[3];
                for (int k = 0; k < 3; k++)
                    w[k] = cos(r) * ref[k] + sin(r) * side[k];
                v3_cross(axis, dir, w);
                v3_norm(axis);
                v3_rotate(dir, axis, b);
                v3_rotate(ref, axis, b);
                v3_rotate(side, axis, b);
            }
        }
    }
    v3_copy(c->end, p);

    /* mitre planes */
    for (int j = 0; j < c->n_joints; j++) {
        PgPiece *a = &c->piece[j], *b = &c->piece[j + 1];
        double n[3];
        v3_add_scaled(n, a->axis, b->axis, 1.0);
        v3_norm(n);
        v3_set(a->n1, v3_dot(n, a->ref), v3_dot(n, a->side), v3_dot(n, a->axis));
        v3_set(b->n0, v3_dot(n, b->ref), v3_dot(n, b->side), v3_dot(n, b->axis));
        a->cut1_deg = acos(fmin(1.0, a->n1[2])) / DEG;
        b->cut0_deg = acos(fmin(1.0, b->n0[2])) / DEG;
        /* the inside of the bend is the direction the plane leans towards on
         * the piece before, and away from on the piece after */
        a->bend1_roll = fmod(atan2(a->n1[1], a->n1[0]) / DEG + 360.0, 360.0);
        b->bend0_roll = fmod(atan2(-b->n0[1], -b->n0[0]) / DEG + 360.0, 360.0);
    }
}

/* ---- checks ------------------------------------------------------------ */

static bool in_box(const double *p, const double *mn, const double *mx)
{
    return p[0] >= mn[0] && p[0] <= mx[0] && p[1] >= mn[1] && p[1] <= mx[1] &&
           p[2] >= mn[2] && p[2] <= mx[2];
}

static bool hits_engine(const PgStub *s, const double *p)
{
    double r2 = p[0] * p[0] + p[2] * p[2];
    if (p[1] >= s->barrel_y0 && p[1] <= s->head_y1 && r2 < s->barrel_r * s->barrel_r)
        return true;
    return in_box(p, s->case_min, s->case_max);
}

double pg_segment_distance(const double *p0, const double *p1,
                           const double *q0, const double *q1,
                           double *sp, double *sq)
{
    double d1[3], d2[3], r[3];
    v3_sub(d1, p1, p0);
    v3_sub(d2, q1, q0);
    v3_sub(r, p0, q0);
    double a = v3_dot(d1, d1), e = v3_dot(d2, d2), f = v3_dot(d2, r);
    double s, t;
    if (a <= 1e-12 && e <= 1e-12) {
        s = t = 0.0;
    } else if (a <= 1e-12) {
        s = 0.0;
        t = fmin(1.0, fmax(0.0, f / e));
    } else {
        double c = v3_dot(d1, r);
        if (e <= 1e-12) {
            t = 0.0;
            s = fmin(1.0, fmax(0.0, -c / a));
        } else {
            double b = v3_dot(d1, d2), den = a * e - b * b;
            s = den > 1e-12 ? fmin(1.0, fmax(0.0, (b * f - c * e) / den)) : 0.0;
            t = (b * s + f) / e;
            if (t < 0.0) {
                t = 0.0;
                s = fmin(1.0, fmax(0.0, -c / a));
            } else if (t > 1.0) {
                t = 1.0;
                s = fmin(1.0, fmax(0.0, (b - c) / a));
            }
        }
    }
    double cp[3], cq[3], dd[3];
    v3_add_scaled(cp, p0, d1, s);
    v3_add_scaled(cq, q0, d2, t);
    v3_sub(dd, cp, cq);
    *sp = s;
    *sq = t;
    return v3_len(dd);
}

static void check(const PgProject *pr, PgChain *c)
{
    const double t = pr->build.thickness_mm;
    int first_made = -1;
    for (int i = 0; i < c->n_pieces; i++)
        if (c->piece[i].kind != PG_PIECE_DUCT) {
            first_made = i;
            break;
        }

    v3_set(c->bmin, HUGE_VAL, HUGE_VAL, HUGE_VAL);
    v3_set(c->bmax, -HUGE_VAL, -HUGE_VAL, -HUGE_VAL);

    for (int i = 0; i < c->n_pieces; i++) {
        PgPiece *pc = &c->piece[i];
        double rA = pc->d0 / 2.0 + t, rB = pc->d1 / 2.0 + t;
        const int NA = 24;

        double min_len = HUGE_VAL;
        bool box_hit = false, eng_hit = false;
        int rings = (int)ceil(pc->len / 40.0) + 1;
        if (rings < 2) rings = 2;
        if (rings > 40) rings = 40;

        for (int a = 0; a < NA; a++) {
            double psi = 2.0 * M_PI * a / NA;
            double z0 = pg_piece_end_z(pc, rA, rB, psi, 0);
            double z1 = pg_piece_end_z(pc, rA, rB, psi, 1);
            if (z1 - z0 < min_len)
                min_len = z1 - z0;
            for (int k = 0; k < rings; k++) {
                double z = z0 + (z1 - z0) * k / (rings - 1);
                double p[3];
                pg_piece_point(pc, rA, rB, psi, z, p);
                for (int m = 0; m < 3; m++) {
                    if (p[m] < c->bmin[m]) c->bmin[m] = p[m];
                    if (p[m] > c->bmax[m]) c->bmax[m] = p[m];
                }
                if (pc->kind == PG_PIECE_DUCT)
                    continue;
                if (pr->clear.enabled) {
                    bool inside = in_box(p, pr->clear.min, pr->clear.max);
                    if (inside != pr->clear.keep_inside)
                        box_hit = true;
                }
                if (i > first_made && hits_engine(&c->stub, p))
                    eng_hit = true;
            }
        }

        if (min_len < 2.0 && pc->kind != PG_PIECE_DUCT) {
            pc->bad_mitre = true;
            c->n_bad_mitre++;
        }
        if (box_hit) { pc->clash = true; c->n_clash_box++; }
        if (eng_hit) { pc->clash = true; c->n_clash_engine++; }
    }

    /* the pipe against itself: pieces far enough apart along the chain that
     * touching can only mean folding back */
    for (int i = 0; i < c->n_pieces; i++) {
        PgPiece *a = &c->piece[i];
        if (a->kind == PG_PIECE_DUCT)
            continue;
        for (int j = i + 2; j < c->n_pieces; j++) {
            PgPiece *b = &c->piece[j];
            double sa, sb;
            double dist = pg_segment_distance(a->p0, a->p1, b->p0, b->p1, &sa, &sb);
            double ra = (a->d0 + (a->d1 - a->d0) * sa) / 2.0 + t;
            double rb = (b->d0 + (b->d1 - b->d0) * sb) / 2.0 + t;
            double along = (b->x0 + sb * b->len) - (a->x0 + sa * a->len);
            if (dist < ra + rb && along > 1.5 * (ra + rb)) {
                if (!a->clash) a->clash = true;
                if (!b->clash) b->clash = true;
                c->n_clash_self++;
            }
        }
    }

    if (c->n_bad_mitre)
        warn(c, "%d piece%s too short for the bends at %s ends: the mitre "
                "cuts cross. Bend less, or use fewer segments there.",
             c->n_bad_mitre, c->n_bad_mitre == 1 ? " is" : "s are",
             c->n_bad_mitre == 1 ? "its" : "their");
    if (c->n_clash_box)
        warn(c, "%d piece%s %s the clearance box.", c->n_clash_box,
             c->n_clash_box == 1 ? "" : "s",
             pr->clear.keep_inside ? "leave" : "enter");
    if (c->n_clash_engine)
        warn(c, "%d piece%s run%s into the engine.", c->n_clash_engine,
             c->n_clash_engine == 1 ? "" : "s", c->n_clash_engine == 1 ? "s" : "");
    if (c->n_clash_self)
        warn(c, "The chamber runs into itself (%d place%s).", c->n_clash_self,
             c->n_clash_self == 1 ? "" : "s");
}

/* ---- bend rules -------------------------------------------------------- */

double pg_section_joint_limit(PgSectionKind kind)
{
    switch (kind) {
    case PG_SEC_HEADER:   return 30.0;
    case PG_SEC_DIFFUSER: return 25.0;
    case PG_SEC_BELLY:    return 20.0;
    case PG_SEC_BAFFLE:   return 20.0;
    case PG_SEC_STINGER:  return 15.0;
    }
    return 20.0;
}

double pg_joint_limit(const PgChain *c, int j)
{
    if (j < 0 || j >= c->n_joints)
        return 0.0;
    const PgJoint *jt = &c->joint[j];
    return fmin(pg_section_joint_limit(c->piece[jt->before].sec_kind),
                pg_section_joint_limit(c->piece[jt->after].sec_kind));
}

double pg_piece_bend_radius_d(const PgChain *c, int i)
{
    if (i <= 0 || i >= c->n_pieces - 1)
        return HUGE_VAL;
    double a = c->joint[i - 1].bend_deg, b = c->joint[i].bend_deg;
    if (a < 1e-6 || b < 1e-6)
        return HUGE_VAL;
    const PgPiece *pc = &c->piece[i];
    double R = pc->len / (tan(a * DEG / 2.0) + tan(b * DEG / 2.0));
    double D = fmax(pc->d0, pc->d1) + 2.0 * c->thickness;
    return R / D;
}

static void joint_rules(PgChain *c)
{
    c->n_sharp = 0;
    for (int j = 0; j < c->n_joints; j++) {
        c->joint[j].limit_deg = pg_joint_limit(c, j);
        c->joint[j].radius_d = HUGE_VAL;
        c->joint[j].sharp = false;
    }
    for (int i = 1; i + 1 < c->n_pieces; i++) {
        double rr = pg_piece_bend_radius_d(c, i);
        if (rr < c->joint[i - 1].radius_d) c->joint[i - 1].radius_d = rr;
        if (rr < c->joint[i].radius_d)     c->joint[i].radius_d = rr;
    }
    for (int j = 0; j < c->n_joints; j++) {
        PgJoint *jt = &c->joint[j];
        if (jt->bend_deg < 1e-6)
            continue;
        jt->sharp = jt->bend_deg > jt->limit_deg + 1e-6 ||
                    jt->radius_d < PG_MIN_BEND_RADIUS_D - 1e-9;
        if (jt->sharp)
            c->n_sharp++;
    }
    if (c->n_sharp)
        warn(c, "%d joint%s bent too sharply: at most 20-30 deg a joint (by "
                "section), and runs of bends no tighter than %.0f diameters. "
                "Spread the turn over more joints, or use Auto-fold.",
             c->n_sharp, c->n_sharp == 1 ? " is" : "s are", PG_MIN_BEND_RADIUS_D);
}

void pg_chain_layout(const PgProject *p, const PgDesign *d, PgChain *c)
{
    memset(c, 0, sizeof *c);
    c->thickness = p->build.thickness_mm;
    pg_engine_stub(&p->engine, &c->stub);
    build_pieces(p, d, c);
    assign_bends(p, c);
    place_pieces(p, c);
    joint_rules(c);
}

void pg_chain_build(const PgProject *p, const PgDesign *d, PgChain *c)
{
    pg_chain_layout(p, d, c);
    check(p, c);
}
