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
#include "pg_view3d.h"
#include "pg_vec.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG (M_PI / 180.0)
#define AROUND 48                      /* facets round a piece */

const PgViewStyle PG_VIEW_DARK = {
    .bg_top     = {  34,  38,  46 }, .bg_bottom  = {  16,  18,  22 },
    .grid       = {  44,  49,  58 }, .grid_major = {  62,  69,  80 },
    .select     = { 255, 196,  84 }, .hover      = { 120, 180, 255 },
    .clash      = { 235,  70,  60 }, .bend       = {  80, 200, 140 },
    .joint      = {  70,  76,  88 }, .seam       = {  40,  44,  52 },
    .box        = { 120, 190, 255 }, .engine     = { 112, 116, 124 },
    .sharp      = { 240, 170,  50 },
};

const PgViewStyle PG_VIEW_LIGHT = {
    .bg_top     = { 250, 251, 253 }, .bg_bottom  = { 222, 227, 235 },
    .grid       = { 214, 219, 227 }, .grid_major = { 190, 197, 208 },
    .select     = { 214, 140,  10 }, .hover      = {  40, 120, 220 },
    .clash      = { 210,  40,  32 }, .bend       = {  24, 150,  90 },
    .joint      = {  92, 100, 114 }, .seam       = {  96, 104, 118 },
    .box        = {  30, 110, 210 }, .engine     = { 150, 154, 162 },
    .sharp      = { 206, 128,   0 },
};

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */

void pg_camera_frame(const PgCamera *cam, int w, int h, PgViewFrame *b)
{
    double cp = cos(cam->pitch), sp = sin(cam->pitch);
    double off[3] = { cp * sin(cam->yaw), sp, cp * cos(cam->yaw) };
    v3_add_scaled(b->eye, cam->target, off, cam->dist);
    v3_set(b->f, -off[0], -off[1], -off[2]);
    double up[3] = { 0.0, 1.0, 0.0 };
    v3_cross(b->r, b->f, up);
    if (v3_len(b->r) < 1e-9)
        v3_set(b->r, 1.0, 0.0, 0.0);
    v3_norm(b->r);
    v3_cross(b->u, b->r, b->f);
    b->focal = (h / 2.0) / tan(cam->fov / 2.0);
    b->cx = w / 2.0;
    b->cy = h / 2.0;
    b->near_z = fmax(1.0, cam->dist * 0.002);

    /* key light from above, over the viewer's shoulder */
    for (int i = 0; i < 3; i++)
        b->light[i] = 0.55 * b->u[i] - 0.45 * b->f[i] + 0.25 * b->r[i];
    b->light[1] += 0.5;
    v3_norm(b->light);
}

void pg_camera_default(PgCamera *cam)
{
    memset(cam, 0, sizeof *cam);
    cam->fov = 35.0 * DEG;
    cam->dist = 4000.0;
    v3_set(cam->target, 1500.0, 0.0, 0.0);
    pg_camera_preset(cam, PG_VIEW_3Q);
}

void pg_camera_preset(PgCamera *cam, PgViewPreset v)
{
    switch (v) {
    case PG_VIEW_3Q:   cam->yaw = 35.0 * DEG; cam->pitch = 24.0 * DEG; break;
    case PG_VIEW_SIDE: cam->yaw = 0.0;        cam->pitch = 0.0;        break;
    case PG_VIEW_TOP:  cam->yaw = 0.0;        cam->pitch = 89.5 * DEG; break;
    case PG_VIEW_END:  cam->yaw = 90.0 * DEG; cam->pitch = 8.0 * DEG;  break;
    }
}

void pg_camera_fit(PgCamera *cam, const PgChain *c, bool with_engine,
                   double aspect)
{
    double mn[3], mx[3];
    v3_copy(mn, c->bmin);
    v3_copy(mx, c->bmax);
    if (with_engine) {
        const PgStub *s = &c->stub;
        double emn[3] = { -s->barrel_r, s->case_min[1], s->case_min[2] };
        double emx[3] = { s->barrel_r, s->head_y1, s->case_max[2] };
        for (int i = 0; i < 3; i++) {
            if (emn[i] < mn[i]) mn[i] = emn[i];
            if (emx[i] > mx[i]) mx[i] = emx[i];
        }
    }
    double centre[3], half[3];
    for (int i = 0; i < 3; i++) {
        centre[i] = (mn[i] + mx[i]) / 2.0;
        half[i] = (mx[i] - mn[i]) / 2.0;
    }
    double radius = v3_len(half);
    if (radius < 50.0)
        radius = 50.0;

    double half_fov = cam->fov / 2.0;
    if (aspect > 0.0 && aspect < 1.0)
        half_fov = atan(tan(half_fov) * aspect);
    v3_copy(cam->target, centre);
    cam->dist = radius / sin(half_fov);

    /* A bounding sphere frames a long thin chamber with half the view
     * empty. Refine against the box's corners as they actually project,
     * so the chamber fills about 85% of whichever dimension binds. */
    const int H = 1000;
    int W = (int)(H * (aspect > 0.0 ? aspect : 1.0));
    for (int iter = 0; iter < 4; iter++) {
        double ex = 0.0, ey = 0.0;
        bool ok = true;
        for (int i = 0; i < 8; i++) {
            double p[3] = { (i & 1) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1],
                            (i & 4) ? mx[2] : mn[2] };
            double sx, sy;
            if (!pg_camera_project(cam, W, H, p, &sx, &sy)) {
                ok = false;
                break;
            }
            ex = fmax(ex, fabs(sx - W / 2.0) / (W / 2.0));
            ey = fmax(ey, fabs(sy - H / 2.0) / (H / 2.0));
        }
        double e = fmax(ex, ey);
        if (!ok || e <= 0.0)
            break;
        double k = e / 0.85;
        if (k > 1.0)
            k = 1.0 + (k - 1.0) * 1.1;     /* grow a little faster than needed */
        cam->dist *= fmax(0.3, fmin(2.0, k));
    }
}

void pg_camera_orbit(PgCamera *cam, double dx_px, double dy_px)
{
    cam->yaw -= dx_px * 0.006;
    cam->pitch += dy_px * 0.006;
    double lim = 89.5 * DEG;
    if (cam->pitch > lim) cam->pitch = lim;
    if (cam->pitch < -lim) cam->pitch = -lim;
    cam->yaw = fmod(cam->yaw, 2.0 * M_PI);
}

void pg_camera_pan(PgCamera *cam, double dx_px, double dy_px, int view_h)
{
    PgViewFrame b;
    pg_camera_frame(cam, 1, view_h, &b);
    double wpp = 2.0 * cam->dist * tan(cam->fov / 2.0) / view_h;
    v3_add_scaled(cam->target, cam->target, b.r, -dx_px * wpp);
    v3_add_scaled(cam->target, cam->target, b.u, dy_px * wpp);
}

void pg_camera_zoom_at(PgCamera *cam, double factor, double sx, double sy,
                       int view_w, int view_h)
{
    PgViewFrame b;
    pg_camera_frame(cam, view_w, view_h, &b);
    double wpp = 2.0 * cam->dist * tan(cam->fov / 2.0) / view_h;
    double p[3];
    v3_add_scaled(p, cam->target, b.r, (sx - view_w / 2.0) * wpp);
    v3_add_scaled(p, p, b.u, -(sy - view_h / 2.0) * wpp);

    double nd = cam->dist * factor;
    if (nd < 30.0) nd = 30.0;
    if (nd > 200000.0) nd = 200000.0;
    double f = nd / cam->dist;
    for (int i = 0; i < 3; i++)
        cam->target[i] = p[i] + (cam->target[i] - p[i]) * f;
    cam->dist = nd;
}

bool pg_camera_project(const PgCamera *cam, int view_w, int view_h,
                       const double p[3], double *sx, double *sy)
{
    PgViewFrame b;
    pg_camera_frame(cam, view_w, view_h, &b);
    double d[3];
    v3_sub(d, p, b.eye);
    double vz = v3_dot(d, b.f);
    if (vz < b.near_z)
        return false;
    *sx = b.cx + v3_dot(d, b.r) / vz * b.focal;
    *sy = b.cy - v3_dot(d, b.u) / vz * b.focal;
    return true;
}

/* ------------------------------------------------------------------ */
/* Building the mesh                                                   */
/* ------------------------------------------------------------------ */

static void *grow(void *buf, int *cap, int need, size_t size, bool *oom)
{
    if (need <= *cap)
        return buf;
    int n = *cap ? *cap : 1024;
    while (n < need)
        n *= 2;
    void *p = realloc(buf, (size_t)n * size);
    if (!p) {
        *oom = true;
        return buf;
    }
    *cap = n;
    return p;
}

static uint8_t byte(float v)
{
    return (uint8_t)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : (int)(v + 0.5f));
}

/* A quad (0,1,2)(0,2,3). No normals: drawn unlit. */
static void mesh_quad(PgMesh *m, const double *p[4], const double *n[4],
                      const float col[3], float alpha, int32_t id)
{
    bool glass = alpha < 1.0f;
    m->vert = grow(m->vert, &m->cap_vert, m->n_vert + 4, sizeof *m->vert, &m->oom);
    if (glass)
        m->glass = grow(m->glass, &m->cap_glass, m->n_glass + 6, sizeof *m->glass, &m->oom);
    else
        m->tri = grow(m->tri, &m->cap_tri, m->n_tri + 6, sizeof *m->tri, &m->oom);
    if (m->oom)
        return;

    uint32_t base = (uint32_t)m->n_vert;
    for (int i = 0; i < 4; i++) {
        PgMeshVert *v = &m->vert[m->n_vert++];
        for (int k = 0; k < 3; k++) {
            v->p[k] = (float)p[i][k];
            v->n[k] = n ? (float)n[i][k] : 0.0f;
            v->col[k] = byte(col[k]);
        }
        v->col[3] = glass ? byte(alpha * 255.0f) : 255;
        v->id = id;
        v->bias = 0.0f;
    }
    const uint32_t idx[6] = { base, base + 1, base + 2, base, base + 2, base + 3 };
    if (glass) {
        memcpy(m->glass + m->n_glass, idx, sizeof idx);
        m->n_glass += 6;
    } else {
        memcpy(m->tri + m->n_tri, idx, sizeof idx);
        m->n_tri += 6;
    }
}

static void mesh_line(PgMesh *m, const double a[3], const double b[3],
                      const uint8_t col[3], float width, int32_t id, float bias)
{
    m->line = grow(m->line, &m->cap_line, m->n_line + 1, sizeof *m->line, &m->oom);
    if (m->oom)
        return;
    PgMeshLine *l = &m->line[m->n_line++];
    for (int k = 0; k < 3; k++) {
        l->a[k] = (float)a[k];
        l->b[k] = (float)b[k];
        l->col[k] = col[k];
    }
    l->col[3] = 255;
    l->id = id;
    l->width = width;
    l->bias = bias;
}

static void mix(float c[3], const uint8_t with[3], float t)
{
    for (int i = 0; i < 3; i++)
        c[i] = c[i] + ((float)with[i] - c[i]) * t;
}

static void section_colour(const PgPiece *pc, float out[3])
{
    /* indexed by PgSectionKind */
    static const float pal[][3] = {
        { 204, 152,  96 },     /* header          */
        {  98, 150, 214 },     /* diffuser        */
        {  96, 186, 172 },     /* belly           */
        { 214, 128,  96 },     /* baffle          */
        { 176, 178, 188 },     /* stinger         */
    };
    int k = (int)pc->sec_kind;
    if (k < 0 || k > 4)
        k = 4;
    memcpy(out, pal[k], sizeof pal[k]);
    /* diffuser stages lighten along the chamber */
    if (pc->sec_kind == PG_SEC_DIFFUSER && pc->stage > 1)
        for (int i = 0; i < 3; i++)
            out[i] = fmin(255.0f, out[i] + 22.0f * (pc->stage - 1));
}

static void piece_colour(const PgPiece *pc, int index, const PgViewOpts *o,
                         const PgViewStyle *st, float out[3])
{
    if (pc->kind == PG_PIECE_DUCT) {
        for (int i = 0; i < 3; i++)
            out[i] = st->engine[i];
        return;
    }
    switch (o->colour) {
    case PG_COLOUR_SECTION:
        section_colour(pc, out);
        break;
    case PG_COLOUR_PART: {
        static const float a[3] = { 196, 202, 212 }, b[3] = { 140, 150, 168 };
        memcpy(out, (index % 2) ? b : a, sizeof a);
        if (pc->kind == PG_PIECE_TUBE) {
            out[0] = 186; out[1] = 166; out[2] = 120;
        }
        break;
    }
    default:
        out[0] = 186; out[1] = 190; out[2] = 198;
    }

    if (o->highlight_section >= 0 && pc->section == o->highlight_section)
        mix(out, st->hover, 0.45f);
    if (pc->clash || pc->bad_mitre)
        mix(out, st->clash, 0.6f);
    if (o->hover_id == index)
        mix(out, st->hover, 0.35f);
    if (o->select_id == index)
        mix(out, st->select, 0.55f);
}

static void build_piece(PgMesh *m, const PgProject *pr, const PgChain *c,
                        int index, const PgViewOpts *o, const PgViewStyle *st)
{
    const PgPiece *pc = &c->piece[index];
    double t = pr->build.thickness_mm;
    double rA = pc->d0 / 2.0 + t, rB = pc->d1 / 2.0 + t;
    double k = pc->len > 0.0 ? (rB - rA) / pc->len : 0.0;

    float base[3];
    piece_colour(pc, index, o, st, base);

    double dmax = 2.0 * fmax(rA, rB);
    int rings = (int)ceil(pc->len / fmax(25.0, 0.6 * dmax));
    if (rings < 1) rings = 1;
    if (rings > 24) rings = 24;

    enum { MAXR = 25 };
    static double P[MAXR][AROUND + 1][3], N[MAXR][AROUND + 1][3];
    for (int a = 0; a <= AROUND; a++) {
        double psi = 2.0 * M_PI * a / AROUND;
        double z0 = pg_piece_end_z(pc, rA, rB, psi, 0);
        double z1 = pg_piece_end_z(pc, rA, rB, psi, 1);
        double c0 = cos(psi), s0 = sin(psi);
        double nrm[3];
        for (int q = 0; q < 3; q++)
            nrm[q] = pc->ref[q] * c0 + pc->side[q] * s0 - pc->axis[q] * k;
        v3_norm(nrm);
        for (int j = 0; j <= rings; j++) {
            double z = z0 + (z1 - z0) * j / rings;
            pg_piece_point(pc, rA, rB, psi, z, P[j][a]);
            v3_copy(N[j][a], nrm);
        }
    }
    for (int j = 0; j < rings; j++) {
        for (int a = 0; a < AROUND; a++) {
            const double *p[4] = { P[j][a], P[j + 1][a], P[j + 1][a + 1], P[j][a + 1] };
            const double *n[4] = { N[j][a], N[j + 1][a], N[j + 1][a + 1], N[j][a + 1] };
            mesh_quad(m, p, n, base, 1.0f, index);
        }
    }

    if (pc->kind == PG_PIECE_DUCT)
        return;

    /* seams */
    if (o->show_seams && pr->build.method == PG_MFG_ROLLED &&
        pc->kind == PG_PIECE_SHEET) {
        double psi = pr->route.seam_deg * DEG;
        double z0 = pg_piece_end_z(pc, rA, rB, psi, 0);
        double z1 = pg_piece_end_z(pc, rA, rB, psi, 1);
        double a3[3], b3[3];
        pg_piece_point(pc, rA + 0.4, rB + 0.4, psi, z0, a3);
        pg_piece_point(pc, rA + 0.4, rB + 0.4, psi, z1, b3);
        mesh_line(m, a3, b3, st->seam, 1.6f, index, 0.002f);
    }

    /* hydroforming flanges: thin fins along both seams */
    if (pr->build.method == PG_MFG_HYDRO && pc->kind == PG_PIECE_SHEET) {
        double fin = pr->build.hydro_margin_mm + 2.0;
        float fcol[3] = { base[0] * 0.8f, base[1] * 0.8f, base[2] * 0.8f };
        for (int sgn = 0; sgn < 2; sgn++) {
            double psi = (pr->route.seam_deg + 90.0 + 180.0 * sgn) * DEG;
            double z0 = pg_piece_end_z(pc, rA, rB, psi, 0);
            double z1 = pg_piece_end_z(pc, rA, rB, psi, 1);
            double p0[3], p1[3], p2[3], p3[3], nrm[3];
            pg_piece_point(pc, rA, rB, psi, z0, p0);
            pg_piece_point(pc, rA, rB, psi, z1, p1);
            pg_piece_point(pc, rA + fin, rB + fin, psi, z1, p2);
            pg_piece_point(pc, rA + fin, rB + fin, psi, z0, p3);
            double e1[3], e2[3];
            v3_sub(e1, p1, p0);
            v3_sub(e2, p3, p0);
            v3_cross(nrm, e1, e2);
            v3_norm(nrm);
            const double *pp[4] = { p0, p1, p2, p3 };
            const double *nn[4] = { nrm, nrm, nrm, nrm };
            mesh_quad(m, pp, nn, fcol, 1.0f, index);
        }
    }
}

static void build_joints(PgMesh *m, const PgProject *pr, const PgChain *c,
                         const PgViewOpts *o, const PgViewStyle *st)
{
    double t = pr->build.thickness_mm;
    for (int j = 0; j < c->n_joints; j++) {
        const PgJoint *jt = &c->joint[j];
        const PgPiece *pc = &c->piece[jt->before];
        int32_t id = PG_PICK_JOINT + j;
        double rA = pc->d0 / 2.0 + t + 0.8, rB = pc->d1 / 2.0 + t + 0.8;

        const uint8_t *col = st->joint;
        float w = 2.4f;
        if (jt->bend_deg > 1e-6) { col = jt->sharp ? st->sharp : st->bend; w = 3.2f; }
        if (o->hover_id == id)   { col = st->hover; w = 4.5f; }
        if (o->select_id == id)  { col = st->select; w = 5.0f; }

        double prev[3];
        for (int a = 0; a <= AROUND; a++) {
            double psi = 2.0 * M_PI * a / AROUND, p[3];
            pg_piece_point(pc, rA, rB, psi, pg_piece_end_z(pc, rA, rB, psi, 1), p);
            if (a)
                mesh_line(m, prev, p, col, w, id, 0.003f);
            v3_copy(prev, p);
        }
    }
}

static void cylinder(PgMesh *m, double r, double y0, double y1, const float col[3],
                     bool caps)
{
    const int N = 40;
    for (int a = 0; a < N; a++) {
        double t0 = 2.0 * M_PI * a / N, t1 = 2.0 * M_PI * (a + 1) / N;
        double p0[3] = { r * cos(t0), y0, r * sin(t0) };
        double p1[3] = { r * cos(t1), y0, r * sin(t1) };
        double p2[3] = { r * cos(t1), y1, r * sin(t1) };
        double p3[3] = { r * cos(t0), y1, r * sin(t0) };
        double n0[3] = { cos(t0), 0, sin(t0) }, n1[3] = { cos(t1), 0, sin(t1) };
        const double *pp[4] = { p0, p1, p2, p3 };
        const double *nn[4] = { n0, n1, n1, n0 };
        mesh_quad(m, pp, nn, col, 1.0f, PG_PICK_ENGINE);
        if (caps) {
            double up[3] = { 0, 1, 0 }, dn[3] = { 0, -1, 0 };
            double c1[3] = { 0, y1, 0 }, c0[3] = { 0, y0, 0 };
            const double *top[4] = { c1, p3, p2, c1 };
            const double *tn[4] = { up, up, up, up };
            mesh_quad(m, top, tn, col, 1.0f, PG_PICK_ENGINE);
            const double *bot[4] = { c0, p1, p0, c0 };
            const double *bn[4] = { dn, dn, dn, dn };
            mesh_quad(m, bot, bn, col, 1.0f, PG_PICK_ENGINE);
        }
    }
}

static void box_solid(PgMesh *m, const double mn[3], const double mx[3],
                      const float col[3], int32_t id, float alpha, bool lit)
{
    static const int face[6][4] = {
        { 0, 1, 3, 2 }, { 4, 6, 7, 5 }, { 0, 4, 5, 1 },
        { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 5, 7, 3 },
    };
    static const double fn[6][3] = {
        { -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
    };
    double v[8][3];
    for (int i = 0; i < 8; i++)
        v3_set(v[i], (i & 4) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1],
               (i & 1) ? mx[2] : mn[2]);
    for (int f = 0; f < 6; f++) {
        const double *pp[4] = { v[face[f][0]], v[face[f][1]], v[face[f][2]], v[face[f][3]] };
        const double *nn[4] = { fn[f], fn[f], fn[f], fn[f] };
        mesh_quad(m, pp, lit ? nn : NULL, col, alpha, id);
    }
}

static void build_engine(PgMesh *m, const PgChain *c, const PgViewStyle *st)
{
    const PgStub *s = &c->stub;
    float col[3] = { st->engine[0], st->engine[1], st->engine[2] };
    float dark[3] = { col[0] * 0.8f, col[1] * 0.8f, col[2] * 0.8f };

    /* barrel core, then cooling fins */
    double core = s->port[0] + 4.0;
    cylinder(m, core, s->barrel_y0, s->barrel_y1, dark, false);
    double pitch = fmax(7.0, (s->barrel_y1 - s->barrel_y0) / 14.0);
    for (double y = s->barrel_y0; y + 2.5 <= s->barrel_y1; y += pitch)
        cylinder(m, s->barrel_r, y, y + 2.5, col, true);

    cylinder(m, s->head_r, s->barrel_y1, s->head_y1, col, true);
    cylinder(m, s->head_r * 0.18, s->head_y1, s->head_y1 + s->head_r * 0.35,
             dark, true);                                   /* spark plug */
    box_solid(m, s->case_min, s->case_max, dark, PG_PICK_ENGINE, 1.0f, true);
}

static void build_grid(PgMesh *m, const PgChain *c, bool with_engine,
                       const PgViewStyle *st)
{
    double y = c->bmin[1];
    if (with_engine && c->stub.case_min[1] < y)
        y = c->stub.case_min[1];
    y -= 2.0;

    double span = fmax(c->bmax[0] - c->bmin[0], c->bmax[2] - c->bmin[2]);
    double step = span > 3000.0 ? 250.0 : span > 1200.0 ? 100.0 : 50.0;
    double x0 = floor((fmin(c->bmin[0], -400.0) - 200.0) / step) * step;
    double x1 = ceil((c->bmax[0] + 200.0) / step) * step;
    double z0 = floor((fmin(c->bmin[2], -400.0) - 200.0) / step) * step;
    double z1 = ceil((fmax(c->bmax[2], 400.0) + 200.0) / step) * step;

    for (double gx = x0; gx <= x1 + 1e-6; gx += step) {
        bool major = fmod(fabs(gx), step * 5.0) < 1e-6;
        double a[3] = { gx, y, z0 }, b[3] = { gx, y, z1 };
        mesh_line(m, a, b, major ? st->grid_major : st->grid, 1.0f, -1, 0.0f);
    }
    for (double gz = z0; gz <= z1 + 1e-6; gz += step) {
        bool major = fmod(fabs(gz), step * 5.0) < 1e-6;
        double a[3] = { x0, y, gz }, b[3] = { x1, y, gz };
        mesh_line(m, a, b, major ? st->grid_major : st->grid, 1.0f, -1, 0.0f);
    }
}

static void build_box(PgMesh *m, const PgProject *pr, const PgChain *c,
                      const PgViewStyle *st)
{
    const double *mn = pr->clear.min, *mx = pr->clear.max;
    const uint8_t *col = c->n_clash_box ? st->clash : st->box;
    double v[8][3];
    for (int i = 0; i < 8; i++)
        v3_set(v[i], (i & 4) ? mx[0] : mn[0], (i & 2) ? mx[1] : mn[1],
               (i & 1) ? mx[2] : mn[2]);
    static const int e[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 },
        { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    };
    for (int i = 0; i < 12; i++)
        mesh_line(m, v[e[i][0]], v[e[i][1]], col, 1.8f, -1, 0.0f);

    float fc[3] = { col[0], col[1], col[2] };
    box_solid(m, mn, mx, fc, -1, 0.07f, false);
}

bool pg_view3d_mesh(PgMesh *m, const PgProject *pr, const PgChain *c,
                    const PgViewOpts *o, const PgViewStyle *st)
{
    m->n_vert = m->n_tri = m->n_glass = m->n_line = 0;
    m->oom = false;

    if (o->show_grid && c->n_pieces > 0)
        build_grid(m, c, o->show_engine, st);
    if (o->show_engine)
        build_engine(m, c, st);
    for (int i = 0; i < c->n_pieces; i++)
        if (o->show_engine || c->piece[i].kind != PG_PIECE_DUCT)
            build_piece(m, pr, c, i, o, st);
    build_joints(m, pr, c, o, st);
    if (o->show_box && pr->clear.enabled)
        build_box(m, pr, c, st);
    return !m->oom;
}

void pg_mesh_free(PgMesh *m)
{
    free(m->vert);
    free(m->tri);
    free(m->glass);
    free(m->line);
    memset(m, 0, sizeof *m);
}

/* ------------------------------------------------------------------ */
/* Drawing it in software                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    PgRaster   *r;
    PgViewFrame b;
} Ctx;

typedef struct {
    double v[3];                        /* view space */
    float  col[3];
} VVert;

static void to_view(const Ctx *x, const double p[3], double v[3])
{
    double d[3];
    v3_sub(d, p, x->b.eye);
    v[0] = v3_dot(d, x->b.r);
    v[1] = v3_dot(d, x->b.u);
    v[2] = v3_dot(d, x->b.f);
}

static void project(const Ctx *x, const VVert *in, PgRVert *out)
{
    double iz = 1.0 / in->v[2];
    out->x = (float)(x->b.cx + in->v[0] * iz * x->b.focal);
    out->y = (float)(x->b.cy - in->v[1] * iz * x->b.focal);
    out->iz = (float)iz;
    out->r = in->col[0];
    out->g = in->col[1];
    out->b = in->col[2];
}

static VVert lerp_near(const VVert *a, const VVert *b, double near)
{
    double t = (near - a->v[2]) / (b->v[2] - a->v[2]);
    VVert o;
    for (int i = 0; i < 3; i++) {
        o.v[i] = a->v[i] + (b->v[i] - a->v[i]) * t;
        o.col[i] = (float)(a->col[i] + (b->col[i] - a->col[i]) * t);
    }
    return o;
}

/* A triangle in view space, clipped against the near plane. */
static void tri_view(Ctx *x, const VVert *t, int32_t id, float alpha, float bias)
{
    double near = x->b.near_z;
    VVert poly[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const VVert *a = &t[i], *b = &t[(i + 1) % 3];
        bool ain = a->v[2] >= near, bin = b->v[2] >= near;
        if (ain)
            poly[n++] = *a;
        if (ain != bin)
            poly[n++] = lerp_near(a, b, near);
    }
    if (n < 3)
        return;
    PgRVert p[4];
    for (int i = 0; i < n; i++)
        project(x, &poly[i], &p[i]);
    pg_raster_tri(x->r, &p[0], &p[1], &p[2], id, alpha, bias);
    if (n == 4)
        pg_raster_tri(x->r, &p[0], &p[2], &p[3], id, alpha, bias);
}

/* Shade a colour at a surface point with normal n. Surfaces seen from behind
 * — the inside of the pipe through an open end — are darkened. */
static void shade(const Ctx *x, const double p[3], const double n_in[3],
                  const float base[3], float out[3])
{
    double n[3], vdir[3];
    v3_copy(n, n_in);
    v3_sub(vdir, x->b.eye, p);
    v3_norm(vdir);
    double inside = 1.0;
    if (v3_dot(n, vdir) < 0.0) {
        n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
        inside = 0.55;
    }
    double diff = fmax(0.0, v3_dot(n, x->b.light));
    double fill = fmax(0.0, v3_dot(n, vdir));
    double h[3];
    v3_add_scaled(h, x->b.light, vdir, 1.0);
    v3_norm(h);
    double spec = pow(fmax(0.0, v3_dot(n, h)), 40.0) * 90.0 * inside;
    double k = (0.26 + 0.58 * diff + 0.24 * fill) * inside;
    for (int i = 0; i < 3; i++)
        out[i] = (float)fmin(255.0, base[i] * k + spec);
}

static void line_view(Ctx *x, const PgMeshLine *l)
{
    VVert v[2];
    double a[3] = { l->a[0], l->a[1], l->a[2] }, b[3] = { l->b[0], l->b[1], l->b[2] };
    to_view(x, a, v[0].v);
    to_view(x, b, v[1].v);
    for (int k = 0; k < 3; k++)
        v[0].col[k] = v[1].col[k] = l->col[k];
    double near = x->b.near_z;
    if (v[0].v[2] < near && v[1].v[2] < near)
        return;
    if (v[0].v[2] < near) v[0] = lerp_near(&v[0], &v[1], near);
    if (v[1].v[2] < near) v[1] = lerp_near(&v[1], &v[0], near);
    PgRVert p[2];
    project(x, &v[0], &p[0]);
    project(x, &v[1], &p[1]);
    pg_raster_line(x->r, &p[0], &p[1], l->width, l->id, 1.0f, l->bias);
}

static void tris_view(Ctx *x, const PgMesh *m, const VVert *vv,
                      const uint32_t *idx, int n, bool glass)
{
    for (int k = 0; k + 2 < n; k += 3) {
        const PgMeshVert *first = &m->vert[idx[k]];
        VVert t[3] = { vv[idx[k]], vv[idx[k + 1]], vv[idx[k + 2]] };
        tri_view(x, t, first->id, glass ? first->col[3] / 255.0f : 1.0f, first->bias);
    }
}

void pg_view3d_draw(PgRaster *r, const PgCamera *cam, const PgMesh *m,
                    const PgViewStyle *st)
{
    pg_raster_clear(r, st->bg_top, st->bg_bottom);
    if (m->n_vert == 0 && m->n_line == 0)
        return;

    Ctx x;
    x.r = r;
    pg_camera_frame(cam, r->w, r->h, &x.b);

    /* Transform and light each vertex once; the quads share them. */
    VVert *vv = malloc((size_t)(m->n_vert > 0 ? m->n_vert : 1) * sizeof *vv);
    if (!vv)
        return;
    for (int i = 0; i < m->n_vert; i++) {
        const PgMeshVert *mv = &m->vert[i];
        double p[3] = { mv->p[0], mv->p[1], mv->p[2] };
        double n[3] = { mv->n[0], mv->n[1], mv->n[2] };
        float base[3] = { mv->col[0], mv->col[1], mv->col[2] };
        to_view(&x, p, vv[i].v);
        if (v3_dot(n, n) > 0.25)
            shade(&x, p, n, base, vv[i].col);
        else
            memcpy(vv[i].col, base, sizeof base);
    }

    /* Opaque surfaces, then lines, then glass: the lines only need the
     * surfaces' depth, and glass must go over everything it shows. */
    tris_view(&x, m, vv, m->tri, m->n_tri, false);
    for (int i = 0; i < m->n_line; i++)
        line_view(&x, &m->line[i]);
    tris_view(&x, m, vv, m->glass, m->n_glass, true);
    free(vv);
}

void pg_view3d_render(PgRaster *r, const PgCamera *cam, const PgProject *pr,
                      const PgChain *c, const PgViewOpts *o,
                      const PgViewStyle *st)
{
    PgMesh m = { 0 };
    pg_view3d_mesh(&m, pr, c, o, st);
    pg_view3d_draw(r, cam, &m, st);
    pg_mesh_free(&m);
}
