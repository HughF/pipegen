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
#include "pg_report.h"
#include "pg_pdf.h"
#include "pg_view3d.h"
#include "pg_version.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define A4_W 297.0
#define A4_H 210.0
#define MARGIN 12.0

/* Drawing colours: a print palette, not the screen theme. */
#define INK        30,  34,  42
#define INK_DIM    96, 104, 118
#define INK_FAINT 160, 166, 176
#define RULE      205, 210, 218
#define ACCENT     26, 108, 204
#define METAL     232, 236, 242
#define WARN      178, 110,  10
#define ETCH       24,  96, 200
#define PLUG      196,  48,  40
#define SUCTION    24, 142,  78

static void date_str(char *buf, size_t cap)
{
    PlatDate dt;
    plat_date_now(&dt);
    snprintf(buf, cap, "%04d-%02d-%02d", dt.year, dt.month, dt.day);
}

/* Word-wrapped paragraph from the top down; returns the y below it. */
static double para(PgPdf *pdf, double x, double y, double w, double size,
                   PgPdfFont font, const char *text)
{
    double lh = size / (72.0 / 25.4) * 1.35;
    char line[400];
    size_t ll = 0;
    const char *s = text;

    while (*s) {
        const char *ws = s;
        while (*s && *s != ' ')
            s++;
        size_t wl = (size_t)(s - ws);

        char trial[400];
        snprintf(trial, sizeof trial, "%.*s%s%.*s", (int)ll, line,
                 ll ? " " : "", (int)wl, ws);
        if (ll && pg_pdf_text_width(size, font, trial) > w) {
            line[ll] = '\0';
            y -= lh;
            pg_pdf_text(pdf, x, y, size, font, PG_PDF_LEFT, 0, line);
            snprintf(line, sizeof line, "%.*s", (int)wl, ws);
            ll = strlen(line);
        } else {
            snprintf(line, sizeof line, "%s", trial);
            ll = strlen(line);
        }
        while (*s == ' ')
            s++;
    }
    if (ll) {
        y -= lh;
        pg_pdf_text(pdf, x, y, size, font, PG_PDF_LEFT, 0, line);
    }
    return y;
}

static void title_block(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                        double w, double h, const char *page_title,
                        int page, int pages)
{
    char buf[256], date[16];
    date_str(date, sizeof date);

    pg_pdf_fill_rgb(pdf, INK);
    pg_pdf_text(pdf, MARGIN, h - MARGIN - 5.0, 15, PG_PDF_BOLD, PG_PDF_LEFT, 0,
                pr->title[0] ? pr->title : "Untitled chamber");
    pg_pdf_fill_rgb(pdf, INK_DIM);
    pg_pdf_text(pdf, w - MARGIN, h - MARGIN - 5.0, 10, PG_PDF_BOLD,
                PG_PDF_RIGHT, 0, page_title);

    snprintf(buf, sizeof buf,
             "%s  \xc2\xb7  %.0f cc, %d cyl  \xc2\xb7  %s at %.0f rpm  \xc2\xb7  "
             "%s, %.1f mm %s",
             pr->engine.name, d->displacement_cc, pr->engine.cylinders,
             pg_objective_name(pr->tuning.objective), d->design_rpm,
             pg_method_name(pr->build.method), pr->build.thickness_mm,
             pg_material_name(pr->build.material));
    pg_pdf_text(pdf, MARGIN, h - MARGIN - 11.0, 8.5, PG_PDF_REGULAR,
                PG_PDF_LEFT, 0, buf);

    pg_pdf_stroke_rgb(pdf, RULE);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_segment(pdf, MARGIN, h - MARGIN - 14.0, w - MARGIN, h - MARGIN - 14.0);
    pg_pdf_segment(pdf, MARGIN, MARGIN + 4.0, w - MARGIN, MARGIN + 4.0);

    pg_pdf_fill_rgb(pdf, INK_FAINT);
    snprintf(buf, sizeof buf, "%s %s  \xc2\xb7  %s", PIPEGEN_NAME,
             PIPEGEN_VERSION, date);
    pg_pdf_text(pdf, MARGIN, MARGIN, 7, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);
    if (pages > 0) {
        snprintf(buf, sizeof buf, "page %d of %d", page, pages);
        pg_pdf_text(pdf, w - MARGIN, MARGIN, 7, PG_PDF_REGULAR,
                    PG_PDF_RIGHT, 0, buf);
    }
    pg_pdf_text(pdf, w / 2.0, MARGIN, 7, PG_PDF_REGULAR, PG_PDF_CENTRE, 0,
                "All dimensions in mm. Diameters are internal unless stated.");
}

/* ------------------------------------------------------------------ */
/* Page 1 — design sheet                                               */
/* ------------------------------------------------------------------ */

static void profile(PgPdf *pdf, const PgDesign *d, double x, double y,
                    double w, double h)
{
    double sx = w / d->total_len;
    double sy_fit = (h * 0.62) / d->d_belly;
    double sy = sy_fit < sx ? sx : sy_fit;       /* never compress */
    double cy = y + h * 0.52;

    /* chamber, filled, upper and lower walls */
    pg_pdf_fill_rgb(pdf, METAL);
    pg_pdf_stroke_rgb(pdf, INK);
    pg_pdf_line_width(pdf, 0.35);
    pg_pdf_move(pdf, x, cy + d->sec[0].d0 / 2.0 * sy);
    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        pg_pdf_line(pdf, x + (s->x0 + s->len) * sx, cy + s->d1 / 2.0 * sy);
    }
    for (int i = d->n_sec - 1; i >= 0; i--) {
        const PgSection *s = &d->sec[i];
        pg_pdf_line(pdf, x + (s->x0 + s->len) * sx, cy - s->d1 / 2.0 * sy);
        pg_pdf_line(pdf, x + s->x0 * sx, cy - s->d0 / 2.0 * sy);
    }
    pg_pdf_close(pdf);
    pg_pdf_fill_stroke(pdf);

    /* centre line */
    pg_pdf_stroke_rgb(pdf, INK_FAINT);
    pg_pdf_line_width(pdf, 0.2);
    pg_pdf_dash(pdf, 4.0, 1.5);
    pg_pdf_segment(pdf, x - 3.0, cy, x + w + 3.0, cy);
    pg_pdf_dash(pdf, 0, 0);

    char buf[64];
    double top = cy + d->d_belly / 2.0 * sy;
    double dim_y = y + 3.0;

    for (int i = 0; i <= d->n_sec; i++) {
        double xs = (i < d->n_sec) ? d->sec[i].x0 : d->total_len;
        double dd = (i < d->n_sec) ? d->sec[i].d0 : d->sec[d->n_sec - 1].d1;
        double px = x + xs * sx;

        /* boundary line and its diameter */
        pg_pdf_stroke_rgb(pdf, RULE);
        pg_pdf_line_width(pdf, 0.2);
        pg_pdf_segment(pdf, px, dim_y, px, top + 6.0);
        pg_pdf_fill_rgb(pdf, INK);
        snprintf(buf, sizeof buf, "\xc3\x98%.1f", dd);
        pg_pdf_text(pdf, px + 1.0, top + 7.5, 6.5, PG_PDF_REGULAR,
                    PG_PDF_LEFT, 60, buf);
    }

    for (int i = 0; i < d->n_sec; i++) {
        const PgSection *s = &d->sec[i];
        double x0 = x + s->x0 * sx, x1 = x + (s->x0 + s->len) * sx;
        double mid = (x0 + x1) / 2.0;

        /* length dimension */
        pg_pdf_stroke_rgb(pdf, INK_DIM);
        pg_pdf_line_width(pdf, 0.2);
        pg_pdf_segment(pdf, x0, dim_y + 2.0, x1, dim_y + 2.0);
        pg_pdf_fill_rgb(pdf, INK_DIM);
        snprintf(buf, sizeof buf, "%.0f", s->len);
        pg_pdf_text(pdf, mid, dim_y + 3.2, 6.5, PG_PDF_REGULAR,
                    PG_PDF_CENTRE, 0, buf);

        /* the name only where it fits; the section table names them all */
        pg_pdf_fill_rgb(pdf, INK);
        double nw = pg_pdf_text_width(6.5, PG_PDF_BOLD, s->name);
        if (nw < (x1 - x0) - 1.0)
            pg_pdf_text(pdf, mid, dim_y + 7.5, 6.5, PG_PDF_BOLD,
                        PG_PDF_CENTRE, 0, s->name);
    }

    /* tuned length marker: port to the middle of the baffle */
    double tx = x + d->plug_dist * sx;
    pg_pdf_stroke_rgb(pdf, PLUG);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_dash(pdf, 1.2, 1.0);
    pg_pdf_segment(pdf, tx, cy - d->d_belly / 2.0 * sy - 2.0, tx, top + 2.0);
    pg_pdf_dash(pdf, 0, 0);
    /* inside the chamber, beside its marker, clear of the section names */
    pg_pdf_fill_rgb(pdf, PLUG);
    snprintf(buf, sizeof buf, "tuned length %.0f", d->tuned_len);
    pg_pdf_text(pdf, tx + 1.5, cy + 1.5, 6.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);

    /* below the length dimensions, where no diameter label can reach */
    pg_pdf_fill_rgb(pdf, INK_FAINT);
    snprintf(buf, sizeof buf, "port face at left  \xc2\xb7  vertical scale "
             "\xc3\x97%.1f", sy / sx);
    pg_pdf_text(pdf, x + w, y - 10.0, 6.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
}

typedef struct {
    double x;
    const char *head;
    PgPdfAlign align;
} Col;

static void table_head(PgPdf *pdf, const Col *c, int n, double y, double x0,
                       double x1)
{
    pg_pdf_fill_rgb(pdf, INK_DIM);
    for (int i = 0; i < n; i++)
        pg_pdf_text(pdf, c[i].x, y, 7, PG_PDF_BOLD, c[i].align, 0, c[i].head);
    pg_pdf_stroke_rgb(pdf, RULE);
    pg_pdf_line_width(pdf, 0.25);
    pg_pdf_segment(pdf, x0, y - 1.5, x1, y - 1.5);
}

static void kv(PgPdf *pdf, double x, double y, double w, const char *k,
               const char *v)
{
    pg_pdf_fill_rgb(pdf, INK_DIM);
    pg_pdf_text(pdf, x, y, 8, PG_PDF_REGULAR, PG_PDF_LEFT, 0, k);
    pg_pdf_fill_rgb(pdf, INK);
    pg_pdf_text(pdf, x + w, y, 8, PG_PDF_BOLD, PG_PDF_RIGHT, 0, v);
}

static void page_design(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                        const PgPartList *parts, const PgLayout *lay,
                        int page, int pages)
{
    pg_pdf_page(pdf, A4_W, A4_H);
    title_block(pdf, pr, d, A4_W, A4_H, "DESIGN SHEET", page, pages);

    profile(pdf, d, MARGIN + 4.0, 112.0, A4_W - 2 * MARGIN - 8.0, 64.0);

    /* ---- section table ---- */
    double tx = MARGIN, ty = 94.0;
    Col cols[] = {
        { tx,          "Section",    PG_PDF_LEFT  },
        { tx + 50.0,   "Start",      PG_PDF_RIGHT },
        { tx + 70.0,   "Length",     PG_PDF_RIGHT },
        { tx + 90.0,   "\xc3\x98 in", PG_PDF_RIGHT },
        { tx + 110.0,  "\xc3\x98 out", PG_PDF_RIGHT },
        { tx + 134.0,  "Half-angle", PG_PDF_RIGHT },
    };
    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, tx, ty + 6.0, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Sections");
    table_head(pdf, cols, 6, ty, tx, tx + 136.0);

    char buf[64];
    double ry = ty - 5.5;
    for (int i = 0; i < d->n_sec; i++, ry -= 4.6) {
        const PgSection *s = &d->sec[i];
        pg_pdf_fill_rgb(pdf, INK);
        pg_pdf_text(pdf, cols[0].x, ry, 8, PG_PDF_REGULAR, PG_PDF_LEFT, 0, s->name);
        snprintf(buf, sizeof buf, "%.0f", s->x0);
        pg_pdf_text(pdf, cols[1].x, ry, 8, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
        snprintf(buf, sizeof buf, "%.1f", s->len);
        pg_pdf_text(pdf, cols[2].x, ry, 8, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
        snprintf(buf, sizeof buf, "%.1f", s->d0);
        pg_pdf_text(pdf, cols[3].x, ry, 8, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
        snprintf(buf, sizeof buf, "%.1f", s->d1);
        pg_pdf_text(pdf, cols[4].x, ry, 8, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
        snprintf(buf, sizeof buf, "%.2f\xc2\xb0", pg_section_half_angle(s));
        pg_pdf_text(pdf, cols[5].x, ry, 8, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
    }
    pg_pdf_stroke_rgb(pdf, RULE);
    pg_pdf_segment(pdf, tx, ry + 2.5, tx + 136.0, ry + 2.5);
    pg_pdf_fill_rgb(pdf, INK);
    pg_pdf_text(pdf, cols[0].x, ry - 1.0, 8, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Overall");
    snprintf(buf, sizeof buf, "%.1f", d->total_len);
    pg_pdf_text(pdf, cols[2].x, ry - 1.0, 8, PG_PDF_BOLD, PG_PDF_RIGHT, 0, buf);

    /* ---- key figures ---- */
    double kx = 160.0, kw = A4_W - MARGIN - kx, ky = ty + 6.0;
    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, kx, ky, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Key figures");
    ky -= 6.0;

    char v[96];
    snprintf(v, sizeof v, "%.0f rpm", d->design_rpm);
    kv(pdf, kx, ky, kw, "Design speed", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.0f rpm", d->tuned_rpm);
    kv(pdf, kx, ky, kw, "Pipe tuned for", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.0f \xe2\x80\x93 %.0f rpm", d->band_lo, d->band_hi);
    kv(pdf, kx, ky, kw, "Plugging pulse on time", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.1f mm", d->tuned_len);
    kv(pdf, kx, ky, kw, "Tuned length (port to mid-baffle)", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.0f m/s at %.0f \xc2\xb0""C", d->wave_speed,
             pr->tuning.gas_temp_c);
    kv(pdf, kx, ky, kw, "Wave speed", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.0f\xc2\xb0 / %.0f\xc2\xb0 ATDC (%.0f\xc2\xb0)",
             d->epo_deg, d->epc_deg, pr->engine.exh_duration_deg);
    kv(pdf, kx, ky, kw, "Exhaust opens / closes", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.1f / %.1f / %.1f mm", d->d_outlet, d->d_belly,
             d->d_stinger);
    kv(pdf, kx, ky, kw, "Outlet / belly / stinger \xc3\x98", v); ky -= 4.4;
    snprintf(v, sizeof v, "%.2f L", d->volume_litres);
    kv(pdf, kx, ky, kw, "Chamber volume", v); ky -= 4.4;
    if (d->silencer_litres > 0.0) {
        snprintf(v, sizeof v, "at least %.1f L", d->silencer_litres);
        kv(pdf, kx, ky, kw, "Silencer after the stinger", v); ky -= 4.4;
    }
    snprintf(v, sizeof v, "%.2f m\xc2\xb2, %.1f kg", parts->sheet_area_mm2 / 1e6,
             parts->mass_kg);
    kv(pdf, kx, ky, kw, "Sheet parts", v); ky -= 4.4;
    snprintf(v, sizeof v, "%d \xc3\x97 %.0f \xc3\x97 %.0f", lay->n_sheets,
             lay->sheet_w, lay->sheet_h);
    kv(pdf, kx, ky, kw, "Sheets", v); ky -= 7.0;

    int nw = d->n_warn + parts->n_warn + (lay->n_oversize ? 1 : 0);
    if (nw) {
        pg_pdf_fill_rgb(pdf, WARN);
        pg_pdf_text(pdf, kx, ky, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Check");
        ky -= 1.5;
        for (int i = 0; i < d->n_warn && ky > 30.0; i++)
            ky = para(pdf, kx, ky, kw, 7, PG_PDF_REGULAR, d->warn[i]) - 1.0;
        for (int i = 0; i < parts->n_warn && ky > 30.0; i++)
            ky = para(pdf, kx, ky, kw, 7, PG_PDF_REGULAR, parts->warn[i]) - 1.0;
        if (lay->n_oversize && ky > 30.0)
            ky = para(pdf, kx, ky, kw, 7, PG_PDF_REGULAR,
                      "At least one part is larger than the sheet in every "
                      "orientation. Use more cone segments or a larger sheet.");
    }

    pg_pdf_fill_rgb(pdf, INK_FAINT);
    para(pdf, MARGIN, 26.0, A4_W - 2 * MARGIN, 6.5, PG_PDF_REGULAR,
         "A starting-point design from wave-timing rules (Jennings/Blair "
         "tradition), not a gas-dynamic simulation. Confirm exhaust timing "
         "and outlet bore on the engine itself, and expect to fine-tune "
         "header and stinger length on the dyno.");
}

/* ------------------------------------------------------------------ */
/* Page 2 — the chamber in 3D, and the route                           */
/* ------------------------------------------------------------------ */

static void page_route(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                       const PgChain *c, const PgPartList *parts,
                       int page, int pages)
{
    pg_pdf_page(pdf, A4_W, A4_H);
    title_block(pdf, pr, d, A4_W, A4_H, "3D VIEW AND ROUTE", page, pages);

    /* a rendered view, the same renderer as the Design page, on a print
     * background */
    const int IW = 760, IH = 570;
    double bw = 180.0, bh = bw * IH / IW;
    double bx = MARGIN, by = A4_H - MARGIN - 19.0 - bh;
    PgRaster *r = pg_raster_new(IW * 2, IH * 2);
    unsigned char *px = malloc((size_t)IW * IH * 4);
    if (r && px) {
        PgCamera cam;
        pg_camera_default(&cam);
        pg_camera_fit(&cam, c, true, (double)IW / IH);
        PgViewOpts o = { .colour = PG_COLOUR_SECTION, .show_engine = true,
                         .show_box = true, .show_seams = true, .show_grid = true,
                         .hover_id = -1, .select_id = -1, .highlight_section = -1 };
        PgViewStyle st = PG_VIEW_LIGHT;
        const unsigned char white[3] = { 255, 255, 255 }, pale[3] = { 238, 241, 246 };
        memcpy(st.bg_top, white, 3);
        memcpy(st.bg_bottom, pale, 3);
        pg_view3d_render(r, &cam, pr, c, &o, &st);
        pg_raster_downsample(r, 2, px, IW, IH);
        pg_pdf_image(pdf, bx, by, bw, bh, px, IW, IH);
        pg_pdf_stroke_rgb(pdf, RULE);
        pg_pdf_line_width(pdf, 0.3);
        pg_pdf_rect(pdf, bx, by, bw, bh);
        pg_pdf_stroke(pdf);
    }
    free(px);
    pg_raster_free(r);

    pg_pdf_fill_rgb(pdf, INK_FAINT);
    pg_pdf_text(pdf, bx, by - 4.0, 7, PG_PDF_REGULAR, PG_PDF_LEFT, 0,
                "Engine coordinates: +X out of the exhaust port, +Y up the "
                "cylinder, Z along the crankshaft. The engine is drawn for "
                "scale only.");

    double x = bx + bw + 6.0, w = A4_W - MARGIN - x, y = A4_H - MARGIN - 22.0;
    char buf[160];

    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, x, y, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Envelope");
    y -= 5.5;
    snprintf(buf, sizeof buf, "%.0f \xc3\x97 %.0f \xc3\x97 %.0f mm",
             c->bmax[0] - c->bmin[0], c->bmax[1] - c->bmin[1],
             c->bmax[2] - c->bmin[2]);
    kv(pdf, x, y, w, "Chamber X \xc3\x97 Y \xc3\x97 Z", buf); y -= 4.4;
    snprintf(buf, sizeof buf, "%.0f mm", d->total_len);
    kv(pdf, x, y, w, "Along the centreline", buf); y -= 4.4;
    if (pr->clear.enabled) {
        snprintf(buf, sizeof buf, "%s %.0f\xe2\x80\x93%.0f, %.0f\xe2\x80\x93%.0f, "
                 "%.0f\xe2\x80\x93%.0f", pr->clear.keep_inside ? "in" : "out of",
                 pr->clear.min[0], pr->clear.max[0], pr->clear.min[1],
                 pr->clear.max[1], pr->clear.min[2], pr->clear.max[2]);
        kv(pdf, x, y, w, "Clearance box", buf); y -= 4.4;
    }
    int clashes = c->n_clash_box + c->n_clash_engine + c->n_clash_self + c->n_bad_mitre;
    y -= 1.5;
    if (clashes) {
        pg_pdf_fill_rgb(pdf, PLUG);
        for (int i = 0; i < c->n_warn && y > 60.0; i++)
            y = para(pdf, x, y + 3.0, w, 7, PG_PDF_REGULAR, c->warn[i]) - 2.0;
    } else {
        pg_pdf_fill_rgb(pdf, SUCTION);
        pg_pdf_text(pdf, x, y, 8, PG_PDF_BOLD, PG_PDF_LEFT, 0, "No clashes");
        y -= 4.0;
    }

    y -= 5.0;
    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, x, y, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Bends");
    y -= 5.5;
    int nb = 0;
    for (int j = 0; j < c->n_joints; j++)
        if (c->joint[j].bend_deg > 1e-6)
            nb++;
    if (!nb) {
        pg_pdf_fill_rgb(pdf, INK_DIM);
        y = para(pdf, x, y + 3.0, w, 7.5, PG_PDF_REGULAR,
                 "Straight. Bend it to fit on the Design page.") - 2.0;
    } else {
        Col cols[] = {
            { x,           "Joint",  PG_PDF_LEFT  },
            { x + 30.0,    "At",     PG_PDF_RIGHT },
            { x + 48.0,    "Bend",   PG_PDF_RIGHT },
            { x + w,       "Towards", PG_PDF_RIGHT },
        };
        table_head(pdf, cols, 4, y, x, x + w);
        y -= 5.0;
        for (int j = 0; j < c->n_joints && y > 22.0; j++) {
            const PgJoint *jt = &c->joint[j];
            if (jt->bend_deg < 1e-6)
                continue;
            pg_pdf_fill_rgb(pdf, INK);
            const PgPiece *a = &c->piece[jt->before], *b = &c->piece[jt->after];
            snprintf(buf, sizeof buf, "J%d %s/%s", j + 1,
                     a->part >= 0 ? parts->part[a->part].id : "-",
                     b->part >= 0 ? parts->part[b->part].id : "-");
            pg_pdf_text(pdf, cols[0].x, y, 7.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);
            snprintf(buf, sizeof buf, "%.0f", jt->x);
            pg_pdf_text(pdf, cols[1].x, y, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
            snprintf(buf, sizeof buf, "%.1f\xc2\xb0", jt->bend_deg);
            pg_pdf_text(pdf, cols[2].x, y, 7.5, PG_PDF_BOLD, PG_PDF_RIGHT, 0, buf);
            snprintf(buf, sizeof buf, "%.0f\xc2\xb0 %s", jt->roll_deg,
                     pg_roll_name(jt->roll_deg));
            pg_pdf_text(pdf, cols[3].x, y, 7, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
            y -= 4.2;
        }
        pg_pdf_fill_rgb(pdf, INK_FAINT);
        y = para(pdf, x, y + 2.0, w, 6.5, PG_PDF_REGULAR,
                 "Each joint is mitred at half its bend on both pieces. "
                 "Directions are round the pipe from a reference that points "
                 "down at the port and is carried along without twisting. A "
                 "double tick on the etch layer marks the inside of each "
                 "bend.") - 2.0;
    }
}

/* ------------------------------------------------------------------ */
/* Page 3 — timing and cut list                                        */
/* ------------------------------------------------------------------ */

static void timing_chart(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                         double x, double y, double w, double h)
{
    double r0, r1;
    if (pr->tuning.objective == PG_OBJ_POWER_BAND) {
        r0 = fmin(pr->tuning.rpm_lo, pr->tuning.rpm_hi) * 0.8;
        r1 = fmax(pr->tuning.rpm_lo, pr->tuning.rpm_hi) * 1.2;
    } else {
        r0 = d->design_rpm * 0.6;
        r1 = d->design_rpm * 1.4;
    }
    double a0 = d->epo_deg - 10.0, a1 = 360.0 + 20.0;

#define PX(r) (x + ((r) - r0) / (r1 - r0) * w)
#define PY(a) (y + ((a) - a0) / (a1 - a0) * h)

    /* plugging window */
    pg_pdf_fill_rgb(pdf, 250, 232, 230);
    pg_pdf_rect(pdf, x, PY(d->epc_deg - PG_TIMING_WINDOW_DEG), w,
                PY(d->epc_deg + PG_TIMING_WINDOW_DEG) -
                PY(d->epc_deg - PG_TIMING_WINDOW_DEG));
    pg_pdf_fill(pdf);

    pg_pdf_stroke_rgb(pdf, RULE);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_rect(pdf, x, y, w, h);
    pg_pdf_stroke(pdf);

    char buf[48];
    const struct { double a; const char *name; } events[] = {
        { d->epo_deg, "EPO" }, { 180.0, "BDC" }, { d->epc_deg, "EPC" },
        { 360.0, "TDC" },
    };
    for (int i = 0; i < 4; i++) {
        pg_pdf_stroke_rgb(pdf, INK_FAINT);
        pg_pdf_line_width(pdf, 0.2);
        pg_pdf_dash(pdf, 1.5, 1.0);
        pg_pdf_segment(pdf, x, PY(events[i].a), x + w, PY(events[i].a));
        pg_pdf_dash(pdf, 0, 0);
        pg_pdf_fill_rgb(pdf, INK_DIM);
        snprintf(buf, sizeof buf, "%s %.0f\xc2\xb0", events[i].name, events[i].a);
        pg_pdf_text(pdf, x - 1.5, PY(events[i].a) - 1.0, 6.5, PG_PDF_REGULAR,
                    PG_PDF_RIGHT, 0, buf);
    }

    for (int i = 0; i <= 4; i++) {
        double r = r0 + (r1 - r0) * i / 4.0;
        pg_pdf_fill_rgb(pdf, INK_DIM);
        snprintf(buf, sizeof buf, "%.0f", r);
        pg_pdf_text(pdf, PX(r), y - 4.0, 6.5, PG_PDF_REGULAR, PG_PDF_CENTRE, 0, buf);
    }
    pg_pdf_text(pdf, x + w / 2.0, y - 8.0, 7, PG_PDF_REGULAR, PG_PDF_CENTRE, 0,
                "engine speed, rpm");

    /* design speed */
    pg_pdf_stroke_rgb(pdf, ACCENT);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_segment(pdf, PX(d->design_rpm), y, PX(d->design_rpm), y + h);

    const struct { double dist; int r, g, b; const char *name; } waves[] = {
        { d->plug_dist,    PLUG,    "plugging pulse (baffle)" },
        { d->suction_dist, SUCTION, "suction wave (diffuser)" },
    };
    for (int k = 0; k < 2; k++) {
        pg_pdf_stroke_rgb(pdf, waves[k].r, waves[k].g, waves[k].b);
        pg_pdf_line_width(pdf, 0.5);
        bool started = false;
        for (int i = 0; i <= 80; i++) {
            double r = r0 + (r1 - r0) * i / 80.0;
            double a = pg_return_angle(d, waves[k].dist, r);
            if (a < a0 || a > a1) {
                if (started) { pg_pdf_stroke(pdf); started = false; }
                continue;
            }
            if (!started) { pg_pdf_move(pdf, PX(r), PY(a)); started = true; }
            else pg_pdf_line(pdf, PX(r), PY(a));
        }
        if (started)
            pg_pdf_stroke(pdf);
        pg_pdf_fill_rgb(pdf, waves[k].r, waves[k].g, waves[k].b);
        pg_pdf_text(pdf, x + 3.0, y + h - 5.0 - 4.5 * k, 7, PG_PDF_BOLD,
                    PG_PDF_LEFT, 0, waves[k].name);
    }
#undef PX
#undef PY
}

static int cut_rows(PgPdf *pdf, const PgPartList *parts, int from,
                    double x, double y, double bottom)
{
    Col c[] = {
        { x,         "ID",          PG_PDF_LEFT  },
        { x + 12.0,  "Part",        PG_PDF_LEFT  },
        { x + 76.0,  "Kind",        PG_PDF_LEFT  },
        { x + 110.0, "Qty",         PG_PDF_RIGHT },
        { x + 138.0, "Rolled \xc3\x98 (mean)", PG_PDF_RIGHT },
        { x + 170.0, "Axial",       PG_PDF_RIGHT },
        { x + 228.0, "Flat",        PG_PDF_RIGHT },
        { x + 272.0, "Area m\xc2\xb2", PG_PDF_RIGHT },
    };
    table_head(pdf, c, 8, y, x, x + 273.0);

    char buf[96];
    double ry = y - 5.5;
    int i = from;
    for (; i < parts->n && ry > bottom; i++, ry -= 4.4) {
        const PgPart *p = &parts->part[i];
        pg_pdf_fill_rgb(pdf, INK);
        pg_pdf_text(pdf, c[0].x, ry, 7.5, PG_PDF_BOLD, PG_PDF_LEFT, 0, p->id);
        pg_pdf_text(pdf, c[1].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0, p->name);
        pg_pdf_text(pdf, c[2].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0,
                    pg_part_kind_name(p->kind));
        snprintf(buf, sizeof buf, "%d", p->qty);
        pg_pdf_text(pdf, c[3].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);

        switch (p->kind) {
        case PG_PART_CONE:
            snprintf(buf, sizeof buf, "%.1f to %.1f", p->d_small, p->d_large);
            break;
        case PG_PART_TUBE:
            snprintf(buf, sizeof buf, "tube %.1f OD \xc3\x97 %.1f wall",
                     p->tube_od, (p->tube_od - p->tube_id) / 2.0);
            break;
        case PG_PART_WRAP:
            snprintf(buf, sizeof buf, "%.1f", p->d_small);
            break;
        default:
            snprintf(buf, sizeof buf, "\xe2\x80\x94");
        }
        pg_pdf_text(pdf, c[4].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);

        snprintf(buf, sizeof buf, "%.1f", p->kind == PG_PART_TUBE ? p->tube_len
                                                                  : p->axial_len);
        pg_pdf_text(pdf, c[5].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);

        switch (p->kind) {
        case PG_PART_CONE:
            if (p->mitred)
                snprintf(buf, sizeof buf, "developed, mitres %.1f\xc2\xb0/%.1f\xc2\xb0",
                         p->cut0_deg, p->cut1_deg);
            else
                snprintf(buf, sizeof buf, "R%.1f / R%.1f, %.2f\xc2\xb0",
                         p->r_outer, p->r_inner, p->sweep_deg);
            break;
        case PG_PART_WRAP:
        case PG_PART_PILLOW:
            snprintf(buf, sizeof buf, "%.1f \xc3\x97 %.1f", p->flat_w, p->flat_l);
            break;
        default:
            if (p->mitred)
                snprintf(buf, sizeof buf, "mitres %.1f\xc2\xb0/%.1f\xc2\xb0",
                         p->cut0_deg, p->cut1_deg);
            else
                snprintf(buf, sizeof buf, "cut to length");
        }
        pg_pdf_text(pdf, c[6].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);

        if (p->kind != PG_PART_TUBE)
            snprintf(buf, sizeof buf, "%.3f", p->area_mm2 / 1e6);
        else
            snprintf(buf, sizeof buf, "\xe2\x80\x94");
        pg_pdf_text(pdf, c[7].x, ry, 7.5, PG_PDF_REGULAR, PG_PDF_RIGHT, 0, buf);
    }
    return i;
}

static int page_timing_cutlist(PgPdf *pdf, const PgProject *pr,
                               const PgDesign *d, const PgPartList *parts,
                               int page, int pages)
{
    pg_pdf_page(pdf, A4_W, A4_H);
    title_block(pdf, pr, d, A4_W, A4_H, "WAVE TIMING AND CUT LIST", page, pages);

    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, MARGIN, A4_H - 36.0, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0,
                "When the reflections reach the port");
    timing_chart(pdf, pr, d, MARGIN + 22.0, 118.0, 150.0, 50.0);

    pg_pdf_fill_rgb(pdf, INK_DIM);
    char txt[512];
    snprintf(txt, sizeof txt,
             "Each curve is the crank angle at which a wave launched as the "
             "exhaust port opens gets back to it, reflected from the middle of "
             "the diffuser (suction) or of the baffle (plugging), at the speed "
             "along the bottom. The shaded band is %.0f\xc2\xb0 either side of "
             "exhaust closure: the plugging pulse crosses it between %.0f and "
             "%.0f rpm. The blue line is the design speed. Suction should "
             "arrive while the transfers are open, around BDC.",
             PG_TIMING_WINDOW_DEG, d->band_lo, d->band_hi);
    para(pdf, 196.0, A4_H - 36.0, A4_W - MARGIN - 196.0, 7.5, PG_PDF_REGULAR, txt);

    pg_pdf_fill_rgb(pdf, ACCENT);
    pg_pdf_text(pdf, MARGIN, 102.0, 9, PG_PDF_BOLD, PG_PDF_LEFT, 0, "Cut list");
    return cut_rows(pdf, parts, 0, MARGIN, 96.0, MARGIN + 10.0);
}

static void page_cutlist_more(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                              const PgPartList *parts, int *next,
                              int page, int pages)
{
    pg_pdf_page(pdf, A4_W, A4_H);
    title_block(pdf, pr, d, A4_W, A4_H, "CUT LIST (CONTINUED)", page, pages);
    *next = cut_rows(pdf, parts, *next, MARGIN, A4_H - 36.0, MARGIN + 10.0);
}

/* ------------------------------------------------------------------ */
/* Layout pages                                                        */
/* ------------------------------------------------------------------ */

static void page_layout(PgPdf *pdf, const PgProject *pr, const PgDesign *d,
                        const PgPartList *parts, const PgLayout *lay, int sheet,
                        int page, int pages)
{
    char title[64];
    snprintf(title, sizeof title, "SHEET %d OF %d", sheet + 1, lay->n_sheets);
    pg_pdf_page(pdf, A4_W, A4_H);
    title_block(pdf, pr, d, A4_W, A4_H, title, page, pages);

    /* extent: the sheet, or the part if it overhangs */
    double ew = lay->sheet_w, eh = lay->sheet_h;
    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        if (pl->sheet != sheet)
            continue;
        const PgPart *p = &parts->part[pl->part];
        for (int k = 0; k < p->n; k++) {
            double x, y;
            pg_place_point(pl, p->v[k].x, p->v[k].y, &x, &y);
            if (x > ew) ew = x;
            if (y > eh) eh = y;
        }
    }

    double bx = MARGIN + 4.0, by = MARGIN + 14.0;
    double bw = A4_W - 2 * MARGIN - 8.0, bh = A4_H - 2 * MARGIN - 38.0;
    double s = fmin(bw / ew, bh / eh);
    double ox = bx + (bw - ew * s) / 2.0, oy = by + (bh - eh * s) / 2.0;

    pg_pdf_fill_rgb(pdf, 246, 247, 249);
    pg_pdf_stroke_rgb(pdf, INK_FAINT);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_rect(pdf, ox, oy, lay->sheet_w * s, lay->sheet_h * s);
    pg_pdf_fill_stroke(pdf);

    for (int i = 0; i < lay->n; i++) {
        const PgPlace *pl = &lay->place[i];
        if (pl->sheet != sheet)
            continue;
        const PgPart *p = &parts->part[pl->part];
        PgPdfXf xf = { s, pl->rot, ox + pl->x * s, oy + pl->y * s };

        pg_pdf_fill_rgb(pdf, METAL);
        pg_pdf_stroke_rgb(pdf, INK);
        pg_pdf_line_width(pdf, 0.25);
        pg_pdf_outline(pdf, p->v, p->n, true, &xf);
        pg_pdf_fill_stroke(pdf);

        double lx, ly;
        pg_place_point(pl, p->label_x, p->label_y, &lx, &ly);
        pg_pdf_fill_rgb(pdf, INK);
        pg_pdf_text(pdf, ox + lx * s, oy + ly * s - 1.0, 7, PG_PDF_BOLD,
                    PG_PDF_CENTRE, 0, p->id);
    }

    char buf[200];
    snprintf(buf, sizeof buf, "%.0f \xc3\x97 %.0f mm sheet, %.1f mm %s, drawn at "
             "1:%.0f%s", lay->sheet_w, lay->sheet_h, pr->build.thickness_mm,
             pg_material_name(pr->build.material), 1.0 / s,
             lay->oversize[sheet] ? "  \xe2\x80\x94  PART LARGER THAN THE SHEET" : "");
    pg_pdf_fill_rgb(pdf, lay->oversize[sheet] ? 178 : 96,
                    lay->oversize[sheet] ? 110 : 104,
                    lay->oversize[sheet] ? 10 : 118);
    pg_pdf_text(pdf, MARGIN, A4_H - 32.0, 8, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);
}

/* ------------------------------------------------------------------ */
/* Full-size templates                                                 */
/* ------------------------------------------------------------------ */

static void page_template(PgPdf *pdf, const PgProject *pr, const PgPart *p)
{
    double pw = p->box.maxx - p->box.minx, ph = p->box.maxy - p->box.miny;
    const double side = 20.0, head = 34.0, foot = 22.0;

    double w = fmax(210.0, pw + 2.0 * side);
    double h = fmax(297.0, ph + head + foot + 10.0);
    pg_pdf_page(pdf, w, h);

    double ox = (w - pw) / 2.0 - p->box.minx;
    double oy = foot + 5.0 + (h - head - foot - 10.0 - ph) / 2.0 - p->box.miny;
    PgPdfXf xf = { 1.0, 0.0, ox, oy };

    pg_pdf_stroke_rgb(pdf, 0, 0, 0);
    pg_pdf_line_width(pdf, 0.25);
    pg_pdf_outline(pdf, p->v, p->n, true, &xf);
    pg_pdf_stroke(pdf);

    if (pr->build.etch_marks && p->n_marks) {
        pg_pdf_stroke_rgb(pdf, ETCH);
        pg_pdf_line_width(pdf, 0.25);
        for (int i = 0; i < p->n_marks; i++)
            pg_pdf_segment(pdf, ox + p->marks[i].x0, oy + p->marks[i].y0,
                           ox + p->marks[i].x1, oy + p->marks[i].y1);
    }

    pg_pdf_fill_rgb(pdf, INK);
    pg_pdf_text(pdf, ox + p->label_x, oy + p->label_y - 2.0, 12, PG_PDF_BOLD,
                PG_PDF_CENTRE, 0, p->id);

    char buf[256];
    snprintf(buf, sizeof buf, "%s  %s", p->id, p->name);
    pg_pdf_text(pdf, side, h - 14.0, 13, PG_PDF_BOLD, PG_PDF_LEFT, 0, buf);

    pg_pdf_fill_rgb(pdf, INK_DIM);
    switch (p->kind) {
    case PG_PART_CONE:
        snprintf(buf, sizeof buf,
                 "Qty %d  \xc2\xb7  cone \xc3\x98%.1f to \xc3\x98%.1f mean, %.1f "
                 "long  \xc2\xb7  outer R%.2f, inner R%.2f, %.3f\xc2\xb0 sector, "
                 "slant %.2f",
                 p->qty, p->d_small, p->d_large, p->axial_len, p->r_outer,
                 p->r_inner, p->sweep_deg, p->slant);
        break;
    case PG_PART_WRAP:
        snprintf(buf, sizeof buf,
                 "Qty %d  \xc2\xb7  cylinder \xc3\x98%.1f mean, %.1f long  "
                 "\xc2\xb7  flat %.2f \xc3\x97 %.2f",
                 p->qty, p->d_small, p->axial_len, p->flat_w, p->flat_l);
        break;
    case PG_PART_PILLOW:
        snprintf(buf, sizeof buf,
                 "Qty %d (a pair per chamber)  \xc2\xb7  flat %.1f long, %.1f "
                 "at the widest, %.1f mm weld margin each side",
                 p->qty, p->flat_l, p->flat_w, pr->build.hydro_margin_mm);
        break;
    default:
        buf[0] = '\0';
    }
    pg_pdf_text(pdf, side, h - 21.0, 8.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);
    snprintf(buf, sizeof buf, "%s  \xc2\xb7  %.1f mm %s  \xc2\xb7  %s %s",
             pr->title, pr->build.thickness_mm,
             pg_material_name(pr->build.material), PIPEGEN_NAME, PIPEGEN_VERSION);
    pg_pdf_text(pdf, side, h - 26.5, 7.5, PG_PDF_REGULAR, PG_PDF_LEFT, 0, buf);

    if (p->kind == PG_PART_PILLOW && p->n_st > 0) {
        /* station widths, so the template can be checked with a rule */
        double ty = h - 31.0;
        char row[256] = "Stations (x : full width)  ";
        for (int i = 0; i < p->n_st; i++) {
            char cell[40];
            snprintf(cell, sizeof cell, "%s%.0f : %.1f", i ? "   " : "",
                     p->st_x[i], 2.0 * p->st_hw[i]);
            strncat(row, cell, sizeof row - strlen(row) - 1);
        }
        pg_pdf_text(pdf, side, ty, 7, PG_PDF_REGULAR, PG_PDF_LEFT, 0, row);
    }

    /* the 100 mm check bar */
    double bx = side, by = 12.0;
    pg_pdf_stroke_rgb(pdf, 0, 0, 0);
    pg_pdf_line_width(pdf, 0.3);
    pg_pdf_segment(pdf, bx, by, bx + 100.0, by);
    for (int i = 0; i <= 10; i++) {
        double tl = (i % 5 == 0) ? 3.0 : 1.5;
        pg_pdf_segment(pdf, bx + 10.0 * i, by, bx + 10.0 * i, by + tl);
    }
    pg_pdf_fill_rgb(pdf, INK);
    pg_pdf_text(pdf, bx + 104.0, by - 1.0, 8, PG_PDF_BOLD, PG_PDF_LEFT, 0,
                "100 mm \xe2\x80\x94 print at 100% (actual size) and check this bar");
}

/* ------------------------------------------------------------------ */

static int count_pages(const PgPartList *parts, const PgLayout *lay,
                       const PgReportOpts *o)
{
    int n = 0;
    if (o->design) {
        n += 3;
        int first = 0;
        for (double y = 96.0 - 5.5; y > MARGIN + 10.0; y -= 4.4) first++;
        int more = 0;
        for (double y = A4_H - 36.0 - 5.5; y > MARGIN + 10.0; y -= 4.4) more++;
        if (parts->n > first)
            n += (parts->n - first + more - 1) / more;
    }
    if (o->layouts)
        n += lay->n_sheets;
    return n;
}

bool pg_report_pdf(const char *path, const PgProject *pr, const PgDesign *d,
                   const PgChain *chain, const PgPartList *parts,
                   const PgLayout *lay,
                   const PgReportOpts *opt, char *err, size_t errcap)
{
    if (!opt->design && !opt->layouts && !opt->templates) {
        snprintf(err, errcap, "Choose at least one kind of PDF page.");
        return false;
    }

    PgPdf *pdf = pg_pdf_new();
    if (!pdf) {
        snprintf(err, errcap, "Out of memory.");
        return false;
    }

    /* Numbered pages are the A4 ones; full-size templates are unnumbered,
     * since they are separated and taken to the bench one by one. */
    int pages = count_pages(parts, lay, opt);
    int page = 1;

    if (opt->design) {
        page_design(pdf, pr, d, parts, lay, page++, pages);
        page_route(pdf, pr, d, chain, parts, page++, pages);
        int next = page_timing_cutlist(pdf, pr, d, parts, page++, pages);
        while (next < parts->n)
            page_cutlist_more(pdf, pr, d, parts, &next, page++, pages);
    }
    if (opt->layouts)
        for (int s = 0; s < lay->n_sheets; s++)
            page_layout(pdf, pr, d, parts, lay, s, page++, pages);
    if (opt->templates)
        for (int i = 0; i < parts->n; i++)
            if (parts->part[i].kind != PG_PART_TUBE && parts->part[i].n > 0)
                page_template(pdf, pr, &parts->part[i]);

    bool ok = pg_pdf_save(pdf, path, pr->title, err, errcap);
    pg_pdf_free(pdf);
    return ok;
}
