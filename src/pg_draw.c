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
#include "pg_draw.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

#define S(dc, v) ((v) * (dc)->scale)
#define MAX_PTS 8192

typedef enum { AL_LEFT, AL_CENTRE, AL_RIGHT } Align;

static float text_w(const PgDrawCtx *dc, const char *s)
{
    const struct nk_user_font *f = dc->font;
    return f->width(f->userdata, f->height, s, (int)strlen(s));
}

static void text(const PgDrawCtx *dc, float x, float y, const char *s,
                 struct nk_color col, Align al)
{
    float w = text_w(dc, s), h = dc->font->height;
    if (al == AL_CENTRE) x -= w / 2.0f;
    else if (al == AL_RIGHT) x -= w;
    nk_draw_text(dc->cb, nk_rect(x, y - h / 2.0f, w + 2.0f, h), s,
                 (int)strlen(s), dc->font, nk_rgba(0, 0, 0, 0), col);
}

static struct nk_color alpha(struct nk_color c, nk_byte a)
{
    c.a = a;
    return c;
}

static bool hovering(const PgDrawCtx *dc, struct nk_rect r)
{
    return dc->mouse.x >= r.x && dc->mouse.x < r.x + r.w &&
           dc->mouse.y >= r.y && dc->mouse.y < r.y + r.h;
}

static void frame(const PgDrawCtx *dc, struct nk_rect r)
{
    nk_fill_rect(dc->cb, r, S(dc, 3), dc->theme->plot_bg);
    nk_stroke_rect(dc->cb, r, S(dc, 3), 1.0f, dc->theme->border);
}

/* ------------------------------------------------------------------ */
/* Profile                                                             */
/* ------------------------------------------------------------------ */

int pg_draw_profile(const PgDrawCtx *dc, struct nk_rect r, const PgDesign *d,
                    const PgChain *c, int highlight_section)
{
    const PgTheme *t = dc->theme;
    frame(dc, r);
    if (d->n_sec == 0 || d->total_len <= 0.0)
        return -1;

    float pad_l = S(dc, 20), pad_r = S(dc, 20);
    float pad_t = S(dc, 46), pad_b = S(dc, 58);
    struct nk_rect p = nk_rect(r.x + pad_l, r.y + pad_t,
                               r.w - pad_l - pad_r, r.h - pad_t - pad_b);
    if (p.w < 20 || p.h < 20)
        return -1;

    double sx = p.w / d->total_len;
    double sy = p.h * 0.9 / d->d_belly;
    if (sy < sx) sy = sx;
    float cy = p.y + p.h / 2.0f;

#define X(mm) (p.x + (float)((mm) * sx))
#define Y(mm) (cy - (float)((mm) * sy))

    /* which section is under the pointer */
    int hover = -1;
    if (hovering(dc, r)) {
        double xm = (dc->mouse.x - p.x) / sx;
        pg_design_diameter_at(d, xm, &hover);
    }

    /* each section as a filled quad (all are convex) */
    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        float pts[8] = {
            X(s->x0), Y(s->d0 / 2), X(s->x0 + s->len), Y(s->d1 / 2),
            X(s->x0 + s->len), Y(-s->d1 / 2), X(s->x0), Y(-s->d0 / 2),
        };
        struct nk_color fill = t->panel_alt;
        if (i == highlight_section || i == hover)
            fill = alpha(t->accent, 90);
        nk_fill_polygon(dc->cb, pts, 4, fill);
    }

    /* the cast duct, which is not fabricated */
    if (c && c->n_pieces && c->piece[0].kind == PG_PIECE_DUCT) {
        const PgPiece *dp = &c->piece[0];
        float pts[8] = {
            X(0), Y(dp->d0 / 2), X(dp->len), Y(dp->d1 / 2),
            X(dp->len), Y(-dp->d1 / 2), X(0), Y(-dp->d0 / 2),
        };
        nk_fill_polygon(dc->cb, pts, 4, alpha(t->text_faint, 110));
    }

    /* walls */
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        float pts[2 * (PG_MAX_SECTIONS + 1)];
        int n = 0;
        pts[n++] = X(0);
        pts[n++] = Y(sgn * d->sec[0].d0 / 2);
        for (int i = 0; i < d->n_sec; i++) {
            pts[n++] = X(d->sec[i].x0 + d->sec[i].len);
            pts[n++] = Y(sgn * d->sec[i].d1 / 2);
        }
        nk_stroke_polyline(dc->cb, pts, n / 2, S(dc, 1.6f), t->text);
    }

    /* centre line */
    for (float x = p.x; x < p.x + p.w; x += S(dc, 14))
        nk_stroke_line(dc->cb, x, cy, fmin(x + S(dc, 8), p.x + p.w), cy,
                       1.0f, t->text_faint);

    char buf[64];
    float top = Y(d->d_belly / 2);
    float dim_y = p.y + p.h + S(dc, 14);

    /* piece joints, faintly, so segment lengths can be seen */
    if (c) {
        for (int j = 0; j < c->n_joints; j++) {
            float x = X(c->joint[j].x);
            int s;
            double dd = pg_design_diameter_at(d, c->joint[j].x, &s);
            nk_stroke_line(dc->cb, x, Y(dd / 2), x, Y(-dd / 2), 1.0f,
                           c->joint[j].bend_deg > 1e-6 ? t->ok : t->divider);
        }
    }

    for (int i = 0; i <= d->n_sec; i++) {
        double xs = i < d->n_sec ? d->sec[i].x0 : d->total_len;
        double dd = i < d->n_sec ? d->sec[i].d0 : d->sec[d->n_sec - 1].d1;
        float x = X(xs);
        nk_stroke_line(dc->cb, x, top - S(dc, 6), x, dim_y + S(dc, 6), 1.0f,
                       t->border);
        snprintf(buf, sizeof buf, "\xc3\x98%.1f", dd);
        text(dc, x, top - S(dc, 16), buf, t->text_dim, AL_CENTRE);
    }

    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        float x0 = X(s->x0), x1 = X(s->x0 + s->len), mid = (x0 + x1) / 2.0f;
        nk_stroke_line(dc->cb, x0, dim_y, x1, dim_y, 1.0f, t->text_faint);
        snprintf(buf, sizeof buf, "%.0f", s->len);
        if (text_w(dc, buf) < x1 - x0 - S(dc, 4))
            text(dc, mid, dim_y + S(dc, 11), buf, t->text_dim, AL_CENTRE);
        if (text_w(dc, s->name) < x1 - x0 - S(dc, 4))
            text(dc, mid, dim_y + S(dc, 27), s->name,
                 i == hover ? t->accent : t->text, AL_CENTRE);
    }

    /* tuned length */
    float tx = X(d->plug_dist);
    nk_stroke_line(dc->cb, tx, Y(d->d_belly / 2) - S(dc, 2),
                   tx, Y(-d->d_belly / 2) + S(dc, 2), S(dc, 1.5f), t->trace_a);
    snprintf(buf, sizeof buf, "tuned length %.0f", d->tuned_len);
    text(dc, tx - S(dc, 6), Y(-d->d_belly / 2) + S(dc, 12), buf, t->trace_a,
         AL_RIGHT);

    snprintf(buf, sizeof buf, "port face at left  \xc2\xb7  vertical \xc3\x97%.1f",
             sy / sx);
    text(dc, r.x + S(dc, 10), r.y + S(dc, 12), buf, t->text_faint, AL_LEFT);

    if (hover >= 0) {
        double xm = (dc->mouse.x - p.x) / sx;
        int s;
        double dd = pg_design_diameter_at(d, xm, &s);
        snprintf(buf, sizeof buf, "%s  \xc2\xb7  %.0f mm from the port  \xc2\xb7  "
                 "\xc3\x98%.1f", d->sec[hover].name, xm, dd);
        text(dc, r.x + r.w - S(dc, 10), r.y + S(dc, 12), buf, t->accent, AL_RIGHT);
        nk_stroke_line(dc->cb, dc->mouse.x, p.y, dc->mouse.x, p.y + p.h, 1.0f,
                       alpha(t->accent, 140));
    }
#undef X
#undef Y
    return hover;
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

void pg_draw_timing(const PgDrawCtx *dc, struct nk_rect r, const PgProject *pr,
                    const PgDesign *d)
{
    const PgTheme *t = dc->theme;
    frame(dc, r);

    float pad_l = S(dc, 72), pad_r = S(dc, 18), pad_t = S(dc, 30), pad_b = S(dc, 40);
    struct nk_rect p = nk_rect(r.x + pad_l, r.y + pad_t, r.w - pad_l - pad_r,
                               r.h - pad_t - pad_b);
    if (p.w < 40 || p.h < 40)
        return;

    double r0, r1;
    if (pr->tuning.objective == PG_OBJ_POWER_BAND) {
        r0 = fmin(pr->tuning.rpm_lo, pr->tuning.rpm_hi) * 0.8;
        r1 = fmax(pr->tuning.rpm_lo, pr->tuning.rpm_hi) * 1.2;
    } else {
        r0 = d->design_rpm * 0.6;
        r1 = d->design_rpm * 1.4;
    }
    double a0 = d->epo_deg - 10.0, a1 = 380.0;

#define PX(rpm) (p.x + (float)(((rpm) - r0) / (r1 - r0) * p.w))
#define PY(a)   (p.y + p.h - (float)(((a) - a0) / (a1 - a0) * p.h))

    nk_fill_rect(dc->cb,
                 nk_rect(p.x, PY(d->epc_deg + PG_TIMING_WINDOW_DEG), p.w,
                         PY(d->epc_deg - PG_TIMING_WINDOW_DEG) -
                         PY(d->epc_deg + PG_TIMING_WINDOW_DEG)),
                 0, alpha(t->trace_c, 45));

    char buf[48];
    const struct { double a; const char *name; } ev[] = {
        { d->epo_deg, "EPO" }, { 180.0, "BDC" }, { d->epc_deg, "EPC" },
        { 360.0, "TDC" },
    };
    for (int i = 0; i < 4; i++) {
        float y = PY(ev[i].a);
        for (float x = p.x; x < p.x + p.w; x += S(dc, 9))
            nk_stroke_line(dc->cb, x, y, fmin(x + S(dc, 5), p.x + p.w), y, 1.0f,
                           t->plot_axis);
        snprintf(buf, sizeof buf, "%s %.0f\xc2\xb0", ev[i].name, ev[i].a);
        text(dc, p.x - S(dc, 6), y, buf, t->text_dim, AL_RIGHT);
    }
    for (int i = 0; i <= 4; i++) {
        double rpm = r0 + (r1 - r0) * i / 4.0;
        snprintf(buf, sizeof buf, "%.0f", rpm);
        text(dc, PX(rpm), p.y + p.h + S(dc, 12), buf, t->text_dim, AL_CENTRE);
    }
    text(dc, p.x + p.w / 2, p.y + p.h + S(dc, 28), "engine speed, rpm",
         t->text_faint, AL_CENTRE);

    nk_stroke_line(dc->cb, PX(d->design_rpm), p.y, PX(d->design_rpm), p.y + p.h,
                   S(dc, 1.5f), t->accent);

    const struct { double dist; struct nk_color col; const char *name; } w[] = {
        { d->plug_dist, t->trace_a, "plugging pulse (baffle)" },
        { d->suction_dist, t->trace_b, "suction wave (diffuser)" },
    };
    for (int k = 0; k < 2; k++) {
        float pts[2 * 81];
        int n = 0;
        for (int i = 0; i <= 80; i++) {
            double rpm = r0 + (r1 - r0) * i / 80.0;
            double a = pg_return_angle(d, w[k].dist, rpm);
            if (a < a0 || a > a1)
                continue;
            pts[n++] = PX(rpm);
            pts[n++] = PY(a);
        }
        if (n >= 4)
            nk_stroke_polyline(dc->cb, pts, n / 2, S(dc, 2.0f), w[k].col);
        text(dc, p.x + S(dc, 8), p.y + S(dc, 10) + S(dc, 16) * k, w[k].name,
             w[k].col, AL_LEFT);
    }

    snprintf(buf, sizeof buf, "on time %.0f\xe2\x80\x93%.0f rpm", d->band_lo, d->band_hi);
    text(dc, p.x + p.w - S(dc, 8), p.y + S(dc, 10), buf, t->trace_c, AL_RIGHT);

    if (hovering(dc, p)) {
        double rpm = r0 + (dc->mouse.x - p.x) / p.w * (r1 - r0);
        nk_stroke_line(dc->cb, dc->mouse.x, p.y, dc->mouse.x, p.y + p.h, 1.0f,
                       t->text_faint);
        char h[120];
        snprintf(h, sizeof h, "%.0f rpm: plugging %.0f\xc2\xb0, suction %.0f\xc2\xb0",
                 rpm, pg_return_angle(d, d->plug_dist, rpm),
                 pg_return_angle(d, d->suction_dist, rpm));
        text(dc, r.x + r.w - S(dc, 10), r.y + S(dc, 14), h, t->text, AL_RIGHT);
    }
    (void)w;
#undef PX
#undef PY
}

/* ------------------------------------------------------------------ */
/* Flat parts                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    float  pts[MAX_PTS * 2];
    int    n;
    double rot, s, ox, oy;       /* part-local mm to screen */
} Flat;

static void flat_add(double x, double y, void *user)
{
    Flat *f = user;
    if (f->n >= MAX_PTS)
        return;
    double X, Y;
    pg_xform(x, y, f->rot, 0.0, 0.0, &X, &Y);
    f->pts[f->n * 2] = (float)(f->ox + X * f->s);
    f->pts[f->n * 2 + 1] = (float)(f->oy - Y * f->s);     /* screen y down */
    f->n++;
}

static bool point_in(const Flat *f, float x, float y)
{
    bool in = false;
    for (int i = 0, j = f->n - 1; i < f->n; j = i++) {
        float xi = f->pts[i * 2], yi = f->pts[i * 2 + 1];
        float xj = f->pts[j * 2], yj = f->pts[j * 2 + 1];
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
            in = !in;
    }
    return in;
}

void pg_draw_part(const PgDrawCtx *dc, struct nk_rect r, const PgPart *part,
                  bool marks)
{
    const PgTheme *t = dc->theme;
    frame(dc, r);
    if (!part || part->n == 0) {
        text(dc, r.x + r.w / 2, r.y + r.h / 2,
             part ? "Cut from tube: no flat pattern" : "No part selected",
             t->text_faint, AL_CENTRE);
        return;
    }

    float pad = S(dc, 30);
    double bw = part->box.maxx - part->box.minx;
    double bh = part->box.maxy - part->box.miny;
    double s = fmin((r.w - 2 * pad) / fmax(bw, 1.0), (r.h - 2 * pad) / fmax(bh, 1.0));

    static Flat f;
    f.n = 0;
    f.rot = 0.0;
    f.s = s;
    f.ox = r.x + r.w / 2.0 - (part->box.minx + bw / 2.0) * s;
    f.oy = r.y + r.h / 2.0 + (part->box.miny + bh / 2.0) * s;
    pg_path_flatten(part->v, part->n, true, 1.0 * M_PI / 180.0, flat_add, &f);
    if (f.n >= 3)
        nk_stroke_polyline(dc->cb, f.pts, f.n, S(dc, 1.8f), t->trace_a);

    if (marks) {
        for (int i = 0; i < part->n_marks; i++) {
            const PgLine *m = &part->marks[i];
            nk_stroke_line(dc->cb, (float)(f.ox + m->x0 * s), (float)(f.oy - m->y0 * s),
                           (float)(f.ox + m->x1 * s), (float)(f.oy - m->y1 * s),
                           S(dc, 1.5f), t->trace_b);
        }
    }

    text(dc, (float)(f.ox + part->label_x * s), (float)(f.oy - part->label_y * s),
         part->id, t->text, AL_CENTRE);

    /* scale bar: a round number of mm about a fifth of the width */
    double want = (r.w * 0.2) / s;
    double mag = pow(10.0, floor(log10(want)));
    double bar = want / mag >= 5 ? 5 * mag : want / mag >= 2 ? 2 * mag : mag;
    float bx = r.x + S(dc, 14), by = r.y + r.h - S(dc, 14);
    nk_stroke_line(dc->cb, bx, by, bx + (float)(bar * s), by, S(dc, 2), t->text_dim);
    char buf[32];
    snprintf(buf, sizeof buf, "%.0f mm", bar);
    text(dc, bx + (float)(bar * s) + S(dc, 6), by, buf, t->text_dim, AL_LEFT);
}

int pg_draw_sheet(const PgDrawCtx *dc, struct nk_rect r, const PgPartList *parts,
                  const PgLayout *lay, int sheet, int highlight_part)
{
    const PgTheme *t = dc->theme;
    frame(dc, r);
    if (sheet < 0 || sheet >= lay->n_sheets)
        return -1;

    /* extent: the sheet, or a part that overhangs it */
    double ew = lay->sheet_w, eh = lay->sheet_h;
    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        if (pl->sheet != sheet)
            continue;
        const PgPart *p = &parts->part[pl->part];
        PgVert tmp[PG_PART_VERTS];
        for (int k = 0; k < p->n; k++) {
            pg_place_point(pl, p->v[k].x, p->v[k].y, &tmp[k].x, &tmp[k].y);
            tmp[k].bulge = p->v[k].bulge;
        }
        PgBox b = pg_path_box(tmp, p->n, true);
        if (b.maxx > ew) ew = b.maxx;
        if (b.maxy > eh) eh = b.maxy;
    }

    float pad = S(dc, 16);
    double s = fmin((r.w - 2 * pad) / ew, (r.h - 2 * pad) / eh);
    double ox = r.x + (r.w - ew * s) / 2.0;
    double oy = r.y + (r.h + eh * s) / 2.0;

    nk_fill_rect(dc->cb, nk_rect((float)ox, (float)(oy - lay->sheet_h * s),
                                 (float)(lay->sheet_w * s), (float)(lay->sheet_h * s)),
                 0, t->panel_alt);
    nk_stroke_rect(dc->cb, nk_rect((float)ox, (float)(oy - lay->sheet_h * s),
                                   (float)(lay->sheet_w * s), (float)(lay->sheet_h * s)),
                   0, 1.0f, lay->oversize[sheet] ? t->alarm : t->border);

    int hover = -1;
    static Flat f;
    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        if (pl->sheet != sheet)
            continue;
        const PgPart *p = &parts->part[pl->part];

        f.n = 0;
        f.rot = pl->rot;
        f.s = s;
        f.ox = ox + pl->x * s;
        f.oy = oy - pl->y * s;
        pg_path_flatten(p->v, p->n, true, 3.0 * M_PI / 180.0, flat_add, &f);

        bool over = hovering(dc, r) && point_in(&f, dc->mouse.x, dc->mouse.y);
        if (over)
            hover = pl->part;
        struct nk_color col = (pl->part == highlight_part || over)
                            ? t->accent : t->text_dim;
        if (f.n >= 3)
            nk_stroke_polyline(dc->cb, f.pts, f.n,
                               S(dc, pl->part == highlight_part ? 2.2f : 1.2f), col);

        double lx, ly;
        pg_place_point(pl, p->label_x, p->label_y, &lx, &ly);
        text(dc, (float)(ox + lx * s), (float)(oy - ly * s), p->id, col, AL_CENTRE);
    }
    return hover;
}
