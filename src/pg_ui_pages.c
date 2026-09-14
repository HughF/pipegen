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
 * pg_ui_pages.c — Patterns, Profile, Export and Help
 */
#include "pg_ui_int.h"
#include "pg_help.h"
#include "pg_version.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* Height from the layout cursor to the bottom of the page. Call it after a
 * completed row: nk_widget_bounds then peeks at where the next row starts,
 * where a zero-height probe row would have taken a whole row of its own. */
static float rest_height(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    float top = nk_widget_bounds(c).y;
    float h = r.y + r.h - top - c->style.window.spacing.y;
    return h < S(ui, 120) ? S(ui, 120) : h;
}

/* ------------------------------------------------------------------ */
/* Patterns                                                            */
/* ------------------------------------------------------------------ */

static void part_details(PgUi *ui, const PgPart *p)
{
    const PgModel *m = ui->model;
    const PgTheme *t = ui->theme;
    struct nk_context *c = ui->ctx;

    nk_layout_row_dynamic(c, S(ui, 26), 1);
    char head[96];
    snprintf(head, sizeof head, "%s  %s", p->id, p->name);
    nk_label_colored(c, head, NK_TEXT_LEFT, t->accent);

    ui_info_rowf(ui, "Quantity", "%d", p->qty);
    ui_info_row(ui, "Kind", pg_part_kind_name(p->kind));

    switch (p->kind) {
    case PG_PART_CONE:
        ui_info_rowf(ui, "Rolled to (mean)", "\xc3\x98%.2f \xe2\x80\x93 %.2f",
                     p->d_small, p->d_large);
        ui_info_rowf(ui, "Axial length", "%.2f mm", p->axial_len);
        ui_info_rowf(ui, "Slant height", "%.2f mm", p->slant);
        ui_info_rowf(ui, "Outer radius", "%.2f mm", p->r_outer);
        ui_info_rowf(ui, "Inner radius", "%.2f mm", p->r_inner);
        ui_info_rowf(ui, "Sector", "%.3f\xc2\xb0", p->sweep_deg);
        break;
    case PG_PART_WRAP:
        ui_info_rowf(ui, "Rolled to", "\xc3\x98%.2f mean", p->d_small);
        ui_info_rowf(ui, "Flat", "%.2f \xc3\x97 %.2f mm", p->flat_w, p->flat_l);
        break;
    case PG_PART_PILLOW:
        ui_info_rowf(ui, "Flat length", "%.1f mm", p->flat_l);
        ui_info_rowf(ui, "Widest", "%.1f mm", p->flat_w);
        ui_info_rowf(ui, "Weld margin", "%.1f mm each side",
                     m->project.build.hydro_margin_mm);
        break;
    case PG_PART_TUBE:
        ui_info_rowf(ui, "Tube", "%.1f OD \xc3\x97 %.1f wall", p->tube_od,
                     (p->tube_od - p->tube_id) / 2.0);
        ui_info_rowf(ui, "Cut length", "%.1f mm (longest side)", p->tube_len);
        break;
    }
    if (p->cut0_deg > 1e-6 || p->cut1_deg > 1e-6)
        ui_info_rowf(ui, "Mitres", "%.1f\xc2\xb0 start, %.1f\xc2\xb0 end",
                     p->cut0_deg, p->cut1_deg);
    if (p->kind != PG_PART_TUBE) {
        ui_info_rowf(ui, "Bounding box", "%.1f \xc3\x97 %.1f mm",
                     p->box.maxx - p->box.minx, p->box.maxy - p->box.miny);
        ui_info_rowf(ui, "Area", "%.4f m\xc2\xb2", p->area_mm2 / 1e6);
        double kg = p->area_mm2 / 1e6 * m->project.build.thickness_mm / 1000.0 *
                    pg_material_density(m->project.build.material);
        ui_info_rowf(ui, "Mass each", "%.2f kg", kg);
    }

    if (p->kind == PG_PART_PILLOW && p->n_st > 0) {
        ui_section(ui, "Stations");
        for (int i = 0; i < p->n_st; i++)
            ui_info_rowf(ui, i == 0 ? "x : full width" : "",
                         "%.0f : %.1f mm", p->st_x[i], 2.0 * p->st_hw[i]);
    }

    if (p->mitred && p->kind != PG_PART_TUBE)
        ui_label_wrap(ui, "A mitred edge is developed point by point. The double "
                      "tick marks the inside of the bend.", t->text_faint);
}

void page_patterns(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    const PgModel *m = ui->model;
    const PgPartList *pl = m->parts;
    const PgLayout *lay = m->layout;

    if (ui->part_sel >= pl->n) ui->part_sel = pl->n - 1;
    if (ui->part_sel < 0) ui->part_sel = 0;
    if (ui->sheet_sel >= lay->n_sheets) ui->sheet_sel = lay->n_sheets - 1;
    if (ui->sheet_sel < 0) ui->sheet_sel = 0;

    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 100));
    nk_layout_row_template_push_static(c, S(ui, 100));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_bool a = ui->pattern_tab == 0, b = ui->pattern_tab == 1;
    ui_tip(ui, "Each flat pattern on its own, with its dimensions");
    if (nk_selectable_label(c, "Parts", NK_TEXT_CENTERED, &a) && a)
        ui->pattern_tab = 0;
    ui_tip(ui, "The parts nested on sheets, as the layout DXF has them");
    if (nk_selectable_label(c, "Sheets", NK_TEXT_CENTERED, &b) && b)
        ui->pattern_tab = 1;
    char sum[200];
    snprintf(sum, sizeof sum, "%d parts  \xc2\xb7  %d sheet%s of %.0f \xc3\x97 %.0f  "
             "\xc2\xb7  %.2f m\xc2\xb2  \xc2\xb7  %.1f kg of %.1f mm %s",
             pl->n, lay->n_sheets, lay->n_sheets == 1 ? "" : "s", lay->sheet_w,
             lay->sheet_h, pl->sheet_area_mm2 / 1e6, pl->mass_kg,
             m->project.build.thickness_mm, pg_material_name(m->project.build.material));
    nk_label_colored(c, sum, NK_TEXT_RIGHT, t->text_dim);

    float h = rest_height(ui, r);
    nk_layout_row_template_begin(c, h);
    nk_layout_row_template_push_static(c, S(ui, 290));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 320));
    nk_layout_row_template_end(c);

    if (ui->pattern_tab == 0) {
        if (nk_group_begin(c, "partlist", NK_WINDOW_BORDER)) {
            for (int i = 0; i < pl->n; i++) {
                const PgPart *p = &pl->part[i];
                nk_layout_row_dynamic(c, S(ui, 24), 1);
                char lab[128];
                snprintf(lab, sizeof lab, "%s  %s%s  \xc3\x97%d", p->id, p->name,
                         p->kind == PG_PART_TUBE ? " (tube)" : "", p->qty);
                nk_bool on = i == ui->part_sel;
                if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                    ui->part_sel = i;
            }
            nk_group_end(c);
        }

        struct nk_rect pr = nk_widget_bounds(c);
        nk_skip(c);
        PgDrawCtx dc = ui_draw_ctx(ui, true);
        pg_draw_part(&dc, pr, pl->n ? &pl->part[ui->part_sel] : NULL,
                     m->project.build.etch_marks);

        if (nk_group_begin(c, "partinfo", NK_WINDOW_BORDER)) {
            if (pl->n)
                part_details(ui, &pl->part[ui->part_sel]);
            nk_group_end(c);
        }
    } else {
        if (nk_group_begin(c, "sheetlist", NK_WINDOW_BORDER)) {
            for (int s = 0; s < lay->n_sheets; s++) {
                int count = 0;
                for (int i = 0; i < lay->n; i++)
                    if (lay->place[i].sheet == s)
                        count++;
                nk_layout_row_dynamic(c, S(ui, 24), 1);
                char lab[96];
                snprintf(lab, sizeof lab, "Sheet %d  \xc2\xb7  %d part%s%s", s + 1,
                         count, count == 1 ? "" : "s",
                         lay->oversize[s] ? "  (oversize)" : "");
                nk_bool on = s == ui->sheet_sel;
                if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
                    ui->sheet_sel = s;
            }
            nk_group_end(c);
        }

        struct nk_rect sr = nk_widget_bounds(c);
        nk_skip(c);
        PgDrawCtx dc = ui_draw_ctx(ui, true);
        int hover = pg_draw_sheet(&dc, sr, pl, lay, ui->sheet_sel, ui->part_sel);
        if (hover >= 0) {
            char tipbuf[128];
            snprintf(tipbuf, sizeof tipbuf, "%s  %s — click to show it",
                     pl->part[hover].id, pl->part[hover].name);
            ui_tip_rect(ui, nk_rect(c->input.mouse.pos.x, c->input.mouse.pos.y, 1, 1),
                        tipbuf);
            if (c->input.mouse.buttons[NK_BUTTON_LEFT].clicked &&
                !c->input.mouse.buttons[NK_BUTTON_LEFT].down && ui->dialog == DLG_NONE)
                ui->part_sel = hover;
        }

        if (nk_group_begin(c, "sheetinfo", NK_WINDOW_BORDER)) {
            ui_section(ui, "Sheet");
            ui_info_rowf(ui, "Size", "%.0f \xc3\x97 %.0f mm", lay->sheet_w, lay->sheet_h);
            ui_info_rowf(ui, "Material", "%.1f mm %s", m->project.build.thickness_mm,
                         pg_material_name(m->project.build.material));
            ui_info_rowf(ui, "Gap", "%.0f mm", lay->gap);
            if (lay->n_sheets && lay->oversize[ui->sheet_sel])
                ui_label_wrap(ui, "The part on this sheet is larger than the "
                              "sheet in every orientation.", t->alarm);
            if (lay->n_unplaced)
                ui_label_wrap(ui, "Some parts could not be placed.", t->alarm);
            ui_gap(ui, 4);
            if (pl->n)
                part_details(ui, &pl->part[ui->part_sel]);
            nk_group_end(c);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Profile                                                             */
/* ------------------------------------------------------------------ */

void page_profile(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgModel *m = ui->model;

    nk_layout_row_dynamic(c, S(ui, 22), 1);
    nk_label_colored(c, "Side profile, straightened, to scale along the chamber",
                     NK_TEXT_LEFT, ui->theme->accent);

    float h = rest_height(ui, r);
    float prof_h = floorf(h * 0.52f);
    float gap = c->style.window.spacing.y;

    nk_layout_row_dynamic(c, prof_h, 1);
    struct nk_rect a = nk_widget_bounds(c);
    nk_skip(c);

    nk_layout_row_dynamic(c, S(ui, 22), 1);
    nk_label_colored(c, "When the reflections reach the port", NK_TEXT_LEFT,
                     ui->theme->accent);
    float top = nk_widget_bounds(c).y;
    float tim_h = r.y + r.h - top - gap;
    if (tim_h < S(ui, 120))
        tim_h = S(ui, 120);
    nk_layout_row_dynamic(c, tim_h, 1);
    struct nk_rect b = nk_widget_bounds(c);
    nk_skip(c);

    PgDrawCtx dc = ui_draw_ctx(ui, true);
    pg_draw_profile(&dc, a, &m->design, m->chain, -1);
    pg_draw_timing(&dc, b, &m->project, &m->design);
}

/* ------------------------------------------------------------------ */
/* Export                                                              */
/* ------------------------------------------------------------------ */

static bool opt_check(PgUi *ui, const char *label, const char *tip, bool *v)
{
    bool changed = ui_check(ui, label, tip, v);
    if (changed)
        pg_settings_save(&ui->settings);
    return changed;
}

void page_export(PgUi *ui, struct nk_rect r)
{
    (void)r;
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgSettings *st = &ui->settings;
    const PgModel *m = ui->model;

    ui_section(ui, "Export for cutting");
    ui_label_wrap(ui, "The DXF is for the laser: millimetres, AutoCAD R12, arcs "
                  "as true arcs, with the cut outlines on their own layer. The "
                  "PDF is for the workshop: design sheet, cut list, sheet layouts "
                  "and full-size templates.", t->text_dim);
    ui_gap(ui, 4);

    ui_form_row(ui, ROW_H);
    nk_label_colored(c, "Folder", NK_TEXT_LEFT, t->text_dim);
    ui_tip(ui, "Where the files are written. Created if it does not exist");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, st->export_dir,
                                   sizeof st->export_dir, nk_filter_default);
    if (ui->path[0]) {
        ui_form_row(ui, ROW_H);
        nk_skip(c);
        ui_tip(ui, "Write the exports beside the project file");
        if (nk_button_label(c, "Use the project's folder")) {
            char dir[PLAT_PATH_MAX];
            snprintf(dir, sizeof dir, "%s", ui->path);
            if (plat_path_parent(dir))
                snprintf(st->export_dir, sizeof st->export_dir, "%s", dir);
        }
    }
    ui_form_row(ui, ROW_H);
    nk_label_colored(c, "File name", NK_TEXT_LEFT, t->text_dim);
    ui_tip(ui, "The start of every file name; made safe for any filesystem");
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->export_base,
                                   sizeof ui->export_base, nk_filter_default);

    ui_section(ui, "DXF");
    opt_check(ui, "Layout: every sheet in one file", "<name>.dxf — parts placed "
              "on sheets, side by side", &st->dxf_layout);
    opt_check(ui, "One file per part", "<name>-P01.dxf and so on, each part at "
              "the origin, for a nesting program", &st->dxf_parts);

    ui_section(ui, "PDF");
    opt_check(ui, "Design sheet, timing and cut list", "The first pages of <name>.pdf",
              &st->pdf_design);
    opt_check(ui, "Sheet layouts", "One A4 page per sheet, scaled to fit",
              &st->pdf_layouts);
    opt_check(ui, "Full-size templates", "Every part on its own page at 1:1, "
              "with a 100 mm check bar", &st->pdf_templates);

    char safe[128];
    pg_export_base_name(ui->export_base, safe, sizeof safe);
    int sheet_parts = 0;
    for (int i = 0; i < m->parts->n; i++)
        if (m->parts->part[i].kind != PG_PART_TUBE)
            sheet_parts++;

    ui_section(ui, "Will write");
    char buf[256];
    if (st->dxf_layout) {
        snprintf(buf, sizeof buf, "%s.dxf  \xe2\x80\x94  %d sheet%s", safe,
                 m->layout->n_sheets, m->layout->n_sheets == 1 ? "" : "s");
        ui_info_row(ui, "", buf);
    }
    if (st->dxf_parts) {
        snprintf(buf, sizeof buf, "%s-P01.dxf \xe2\x80\xa6  \xe2\x80\x94  %d files",
                 safe, sheet_parts);
        ui_info_row(ui, "", buf);
    }
    if (st->pdf_design || st->pdf_layouts || st->pdf_templates) {
        snprintf(buf, sizeof buf, "%s.pdf", safe);
        ui_info_row(ui, "", buf);
    }

    ui_gap(ui, 6);
    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_static(c, S(ui, LABEL_W));
    nk_layout_row_template_push_static(c, S(ui, 150));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_skip(c);
    ui_tip(ui, "Write the files now");
    if (ui_primary_button(ui, "Export")) {
        PgExportOpts o = { st->dxf_layout, st->dxf_parts, st->pdf_design,
                           st->pdf_layouts, st->pdf_templates };
        ui->export_ok = pg_export_files(m, st->export_dir, ui->export_base, &o,
                                        ui->export_msg, sizeof ui->export_msg);
        pg_settings_save(st);
        ui_message(ui, !ui->export_ok, "%s", ui->export_msg);
    }
    nk_skip(c);
    if (ui->export_msg[0])
        ui_label_wrap(ui, ui->export_msg, ui->export_ok ? t->ok : t->alarm);

    int clashes = m->chain->n_clash_box + m->chain->n_clash_engine +
                  m->chain->n_clash_self + m->chain->n_bad_mitre;
    if (clashes)
        ui_label_wrap(ui, "The design has clashes (see the Design page). The "
                      "files can still be written.", t->warn);

    ui_section(ui, "DXF layers");
    ui_info_row(ui, "CUT", "Part outlines. The only layer to cut.");
    ui_info_row(ui, "ETCH", "Alignment marks. Mark or etch, do not cut.");
    ui_info_row(ui, "LABEL", "Part numbers. Engrave or ignore.");
    ui_info_row(ui, "SHEET", "Sheet borders and titles. Never cut.");
    ui_label_wrap(ui, "No kerf compensation is applied: set it in the laser's CAM.",
                  t->text_faint);
    ui_gap(ui, 10);
}

/* ------------------------------------------------------------------ */
/* Help                                                                */
/* ------------------------------------------------------------------ */

void page_help(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    (void)r;

    struct nk_rect region = nk_window_get_content_region(c);
    float avail = region.w - c->style.text.padding.x * 2.0f - S(ui, 6);
    float col = S(ui, 760.0f);
    if (col > avail) col = avail;
    if (col < S(ui, 200)) col = S(ui, 200);

    float key_w = S(ui, 200.0f);
    float val_w = col - key_w - c->style.window.spacing.x;
    if (val_w < S(ui, 120)) {
        key_w = col * 0.35f;
        val_w = col - key_w - c->style.window.spacing.x;
    }
    float ind_w = S(ui, 18.0f);
    float bul_w = col - ind_w - c->style.window.spacing.x;

    /* One column `col` wide and nothing after it — a trailing spacer becomes
     * a second full row and riddles the page with holes. */
    #define HELP_ROW1(h)  do {                                       \
        nk_layout_row_template_begin(c, (h));                        \
        nk_layout_row_template_push_static(c, col);                  \
        nk_layout_row_template_end(c);                               \
    } while (0)

    nk_layout_row_dynamic(c, S(ui, 30), 1);
    nk_label_colored(c, PIPEGEN_NAME "  " PIPEGEN_VERSION "  \xe2\x80\x94  manual",
                     NK_TEXT_LEFT, t->accent);
    HELP_ROW1(ui_wrap_height(ui, "x", col));
    nk_label_colored(c, "F1 opens this page. Rest the pointer on any control for a "
                     "one-line version of what it does.", NK_TEXT_LEFT, t->text_dim);

    int n_sec = 0;
    const PgHelpSection *sec = pg_help_sections(&n_sec);
    for (int i = 0; i < n_sec; i++) {
        ui_gap(ui, 12);
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        nk_label_colored(c, sec[i].title, NK_TEXT_LEFT, t->accent);
        if (sec[i].intro) {
            HELP_ROW1(ui_wrap_height(ui, sec[i].intro, col));
            nk_label_colored_wrap(c, sec[i].intro, t->text_dim);
        }
        for (int k = 0; k < sec[i].n_items; k++) {
            const PgHelpItem *e = &sec[i].items[k];
            switch (e->kind) {
            case PG_HELP_TEXT:
                HELP_ROW1(ui_wrap_height(ui, e->a, col));
                nk_label_colored_wrap(c, e->a, t->text_dim);
                break;
            case PG_HELP_SUB:
                ui_gap(ui, 4);
                nk_layout_row_dynamic(c, S(ui, 22), 1);
                nk_label_colored(c, e->a, NK_TEXT_LEFT, t->text);
                break;
            case PG_HELP_BULLET:
                nk_layout_row_template_begin(c, ui_wrap_height(ui, e->a, bul_w));
                nk_layout_row_template_push_static(c, ind_w);
                nk_layout_row_template_push_static(c, bul_w);
                nk_layout_row_template_end(c);
                nk_label_colored(c, "\xc2\xb7", NK_TEXT_ALIGN_RIGHT | NK_TEXT_ALIGN_TOP,
                                 t->text_faint);
                nk_label_colored_wrap(c, e->a, t->text_dim);
                break;
            case PG_HELP_ROW: {
                float hgt = ui_wrap_height(ui, e->b ? e->b : "", val_w);
                float kh = ui_wrap_height(ui, e->a, key_w);
                if (kh > hgt) hgt = kh;
                nk_layout_row_template_begin(c, hgt);
                nk_layout_row_template_push_static(c, key_w);
                nk_layout_row_template_push_static(c, val_w);
                nk_layout_row_template_end(c);
                nk_label_colored_wrap(c, e->a, t->text);
                nk_label_colored_wrap(c, e->b ? e->b : "", t->text_dim);
                break;
            }
            case PG_HELP_NOTE:
                HELP_ROW1(ui_wrap_height(ui, e->a, col));
                nk_label_colored_wrap(c, e->a, t->warn);
                break;
            }
        }
    }
    ui_gap(ui, 14);
    HELP_ROW1(ui_wrap_height(ui, "x", col));
    nk_label_colored(c, "The same text is written to docs/HELP.md by: pipegen --help-doc",
                     NK_TEXT_LEFT, t->text_faint);
    ui_gap(ui, 10);
    #undef HELP_ROW1
}
