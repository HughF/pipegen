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
 * pg_ui_dialogs.c — the New Project wizard, the file browser, the unsaved-
 * changes question and About
 *
 * SDL2 has no native file dialog, and pulling in a toolkit for one would
 * break the SDL-only build, so the browser is drawn here: a folder, its
 * folders and project files, and a name.
 */
#include "pg_ui_int.h"
#include "pg_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* Openers                                                             */
/* ------------------------------------------------------------------ */

void ui_open_wizard(PgUi *ui, const PgProject *seed)
{
    if (seed)
        ui->wiz = *seed;
    else
        pg_project_default(&ui->wiz);
    ui->wiz_step = 0;
    ui->wiz_preset = 0;
    ui->dlg_error[0] = '\0';
    ui->dialog = DLG_WIZARD;
}

static void list_dir(PgUi *ui)
{
    ui->n_entries = plat_dir_list(ui->file_dir, ui->entries, FILE_ENTRIES);
    if (ui->n_entries < 0) {
        ui->n_entries = 0;
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "Cannot read %.180s", ui->file_dir);
    } else {
        ui->dlg_error[0] = '\0';
    }
    ui->file_listed = true;
}

void ui_open_file_dialog(PgUi *ui, FileMode mode)
{
    ui->file_mode = mode;
    const char *dir = ui->settings.project_dir;
    if (!dir[0] || !plat_is_dir(dir)) {
        static char docs[PLAT_PATH_MAX];
        if (!plat_documents_dir(docs, sizeof docs))
            snprintf(docs, sizeof docs, ".");
        dir = docs;
    }
    snprintf(ui->file_dir, sizeof ui->file_dir, "%s", dir);
    if (mode == FILE_SAVE) {
        char base[128];
        pg_export_base_name(ui->model->project.title, base, sizeof base);
        snprintf(ui->file_name, sizeof ui->file_name, "%s", ui->path[0]
                 ? plat_path_leaf(ui->path) : base);
        if (!ui->path[0])
            strncat(ui->file_name, PG_PROJECT_EXT,
                    sizeof ui->file_name - strlen(ui->file_name) - 1);
    } else {
        ui->file_name[0] = '\0';
    }
    list_dir(ui);
    ui->dialog = DLG_FILE;
}

void ui_open_confirm(PgUi *ui, Pending pending)
{
    ui->pending = pending;
    ui->dlg_error[0] = '\0';
    ui->dialog = DLG_CONFIRM;
}

/* ------------------------------------------------------------------ */
/* Dialog chrome                                                       */
/* ------------------------------------------------------------------ */

static DlgResult dialog_buttons(PgUi *ui, bool can_ok, const char *primary,
                                const char *alt)
{
    struct nk_context *c = ui->ctx;
    DlgResult res = DLG_R_NONE;

    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    if (alt)
        nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_end(c);

    nk_skip(c);
    ui_tip(ui, "Close without doing it. Escape does the same");
    if (nk_button_label(c, "Cancel"))
        res = DLG_R_CANCEL;
    if (alt && nk_button_label(c, alt))
        res = DLG_R_ALT;
    if (!can_ok) nk_widget_disable_begin(c);
    if (ui_primary_button(ui, primary) && can_ok)
        res = DLG_R_OK;
    if (!can_ok) nk_widget_disable_end(c);
    return res;
}

/* ------------------------------------------------------------------ */
/* Wizard                                                              */
/* ------------------------------------------------------------------ */

#define WIZ_STEPS 5
static const char *WIZ_NAME[WIZ_STEPS] = {
    "Engine", "Objective", "Manufacture", "Installation", "Review"
};

static void wiz_engine(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    ui_label_wrap(ui, "Start from an engine preset, or enter the engine's own "
                  "figures.", ui->theme->text_dim);

    ui_section(ui, "Preset");
    for (int i = 0; i < pg_engine_preset_count(); i++) {
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        if (nk_option_label(c, pg_engine_preset_name(i), ui->wiz_preset == i) &&
            ui->wiz_preset != i) {
            ui->wiz_preset = i;
            pg_engine_preset(i, &ui->wiz.engine, &ui->wiz.tuning.rpm);
        }
    }
    nk_layout_row_dynamic(c, S(ui, 24), 1);
    if (nk_option_label(c, "Custom", ui->wiz_preset < 0))
        ui->wiz_preset = -1;

    ui_section(ui, "Project");
    ui_form_row(ui, ROW_H);
    nk_label_colored(c, "Title", NK_TEXT_LEFT, ui->theme->text_dim);
    nk_edit_string_zero_terminated(c, NK_EDIT_FIELD, ui->wiz.title,
                                   sizeof ui->wiz.title, nk_filter_default);

    if (form_engine(ui, &ui->wiz, true))
        ui->wiz_preset = -1;
}

static void wiz_install(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgClearance *cl = &ui->wiz.clear;

    ui_label_wrap(ui, "The chamber is designed straight. Once the project is "
                  "created, bend it to fit on the Design page: select a joint "
                  "in the 3D view and turn it.", t->text_dim);

    ui_section(ui, "Clearance box");
    ui_label_wrap(ui, "Optional. Give the space the chamber has to fit in — or "
                  "an obstacle it has to miss — and every piece is checked "
                  "against it. Engine coordinates: +X out of the port, +Y up "
                  "the cylinder, Z along the crankshaft.", t->text_faint);
    ui_check(ui, "Use a clearance box", "Check the chamber against a box",
             &cl->enabled);
    if (cl->enabled) {
        nk_layout_row_dynamic(c, S(ui, 24), 2);
        if (nk_option_label(c, "Keep inside", cl->keep_inside))
            cl->keep_inside = true;
        if (nk_option_label(c, "Keep out", !cl->keep_inside))
            cl->keep_inside = false;
        static const char *axis[3] = { "X", "Y", "Z" };
        for (int i = 0; i < 3; i++) {
            char lab[32], id[16];
            snprintf(lab, sizeof lab, "%s from", axis[i]);
            snprintf(id, sizeof id, "wmin%d", i);
            ui_prop(ui, lab, id, "Box minimum", &cl->min[i], -20000, 20000, 10, "mm");
            snprintf(lab, sizeof lab, "%s to", axis[i]);
            snprintf(id, sizeof id, "wmax%d", i);
            ui_prop(ui, lab, id, "Box maximum", &cl->max[i], -20000, 20000, 10, "mm");
        }
    }
}

static void wiz_review(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgModel *m = ui->wiz_model;

    m->project = ui->wiz;
    pg_project_sanitise(&m->project);
    pg_model_update(m);
    const PgDesign *d = &m->design;

    ui_info_rowf(ui, "Engine", "%s, %.0f cc", m->project.engine.name, d->displacement_cc);
    ui_info_rowf(ui, "Objective", "%s at %.0f rpm",
                 pg_objective_name(m->project.tuning.objective), d->design_rpm);
    ui_info_rowf(ui, "Tuned length", "%.0f mm", d->tuned_len);
    ui_info_rowf(ui, "On time", "%.0f \xe2\x80\x93 %.0f rpm", d->band_lo, d->band_hi);
    ui_info_rowf(ui, "Chamber", "%.0f mm along the centreline, belly \xc3\x98%.0f",
                 d->total_len, d->d_belly);
    ui_info_rowf(ui, "Made", "%s, %d parts on %d sheet%s, %.1f kg",
                 pg_method_name(m->project.build.method), m->parts->n,
                 m->layout->n_sheets, m->layout->n_sheets == 1 ? "" : "s",
                 m->parts->mass_kg);

    nk_layout_row_dynamic(c, S(ui, 190), 1);
    struct nk_rect r = nk_widget_bounds(c);
    nk_skip(c);
    PgDrawCtx dc = ui_draw_ctx(ui, false);
    pg_draw_profile(&dc, r, d, m->chain, -1);

    for (int i = 0; i < d->n_warn; i++)
        ui_label_wrap(ui, d->warn[i], t->warn);
    for (int i = 0; i < m->chain->n_warn; i++)
        ui_label_wrap(ui, m->chain->warn[i], t->warn);
    for (int i = 0; i < m->parts->n_warn; i++)
        ui_label_wrap(ui, m->parts->warn[i], t->warn);
}

static void wiz_body(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    nk_layout_row_dynamic(c, S(ui, 26), WIZ_STEPS);
    for (int i = 0; i < WIZ_STEPS; i++) {
        char lab[48];
        snprintf(lab, sizeof lab, "%d  %s", i + 1, WIZ_NAME[i]);
        nk_label_colored(c, lab, NK_TEXT_CENTERED,
                         i == ui->wiz_step ? t->accent
                         : i < ui->wiz_step ? t->text_dim : t->text_faint);
    }
    ui_gap(ui, 2);

    switch (ui->wiz_step) {
    case 0: wiz_engine(ui); break;
    case 1: form_tuning(ui, &ui->wiz); break;
    case 2: form_build(ui, &ui->wiz); break;
    case 3: wiz_install(ui); break;
    case 4: wiz_review(ui); break;
    }
    ui_gap(ui, 6);
}

static void wiz_buttons(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    nk_layout_row_template_begin(c, S(ui, BTN_H));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_push_static(c, S(ui, BTN_W));
    nk_layout_row_template_end(c);

    nk_skip(c);
    ui_tip(ui, "Leave the wizard; the current project is kept");
    if (nk_button_label(c, "Cancel"))
        ui->dialog = DLG_NONE;
    if (ui->wiz_step == 0) nk_widget_disable_begin(c);
    ui_tip(ui, "The previous step");
    if (nk_button_label(c, "Back") && ui->wiz_step > 0)
        ui->wiz_step--;
    if (ui->wiz_step == 0) nk_widget_disable_end(c);

    if (ui->wiz_step < WIZ_STEPS - 1) {
        ui_tip(ui, "The next step");
        if (ui_primary_button(ui, "Next"))
            ui->wiz_step++;
    } else {
        ui_tip(ui, "Create the project and open it in the Design page");
        if (ui_primary_button(ui, "Create")) {
            ui_set_project(ui, &ui->wiz, NULL);
            ui->page = PAGE_DESIGN;
            ui->dialog = DLG_NONE;
            ui_message(ui, false, "New project created. Save it from the rail.");
        }
    }
}

/* ------------------------------------------------------------------ */
/* File browser                                                        */
/* ------------------------------------------------------------------ */

static bool has_ext(const char *name)
{
    size_t n = strlen(name), e = strlen(PG_PROJECT_EXT);
    return n > e && strcasecmp(name + n - e, PG_PROJECT_EXT) == 0;
}

static bool file_commit(PgUi *ui)
{
    if (!ui->file_name[0]) {
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "Give a file name.");
        return false;
    }
    char name[200];
    snprintf(name, sizeof name, "%s", ui->file_name);
    if (ui->file_mode == FILE_SAVE && !has_ext(name))
        strncat(name, PG_PROJECT_EXT, sizeof name - strlen(name) - 1);

    char path[PLAT_PATH_MAX];
    if (!plat_path_join(path, sizeof path, ui->file_dir, name)) {
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "That path is too long.");
        return false;
    }

    if (ui->file_mode == FILE_OPEN) {
        if (!plat_file_exists(path)) {
            snprintf(ui->dlg_error, sizeof ui->dlg_error, "No such file.");
            return false;
        }
        ui->dialog = DLG_NONE;
        return ui_open_path(ui, path);
    }

    if (!ui_save_to(ui, path)) {
        snprintf(ui->dlg_error, sizeof ui->dlg_error, "%s", ui->msg);
        return false;
    }
    ui->dialog = DLG_NONE;
    if (ui->save_then_pending)
        ui_continue_pending(ui);
    return true;
}

static void file_body(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    if (!ui->file_listed)
        list_dir(ui);

    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 60));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, 60));
    nk_layout_row_template_end(c);
    nk_label_colored(c, "Folder", NK_TEXT_LEFT, t->text_dim);
    ui_tip(ui, "Type a folder and press Enter");
    nk_flags f = nk_edit_string_zero_terminated(c, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                                                ui->file_dir, sizeof ui->file_dir,
                                                nk_filter_default);
    if (f & NK_EDIT_COMMITED)
        list_dir(ui);
    ui_tip(ui, "Up one folder");
    if (nk_button_label(c, "Up")) {
        if (plat_path_parent(ui->file_dir))
            list_dir(ui);
    }

    /* recent projects first, when opening */
    if (ui->file_mode == FILE_OPEN && ui->settings.recent[0][0]) {
        ui_section(ui, "Recent");
        for (int i = 0; i < PG_RECENT; i++) {
            const char *rp = ui->settings.recent[i];
            if (!rp[0] || !plat_file_exists(rp))
                continue;
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            ui_tip(ui, rp);
            if (nk_button_label(c, plat_path_leaf(rp))) {
                ui->dialog = DLG_NONE;
                ui_open_path(ui, rp);
                return;
            }
        }
    }

    ui_section(ui, ui->file_mode == FILE_OPEN ? "Projects" : "In this folder");
    nk_layout_row_dynamic(c, S(ui, 250), 1);
    if (nk_group_begin(c, "files", NK_WINDOW_BORDER)) {
        int shown = 0;
        for (int i = 0; i < ui->n_entries; i++) {
            PlatDirEntry *e = &ui->entries[i];
            if (!e->is_dir && !has_ext(e->name))
                continue;
            shown++;
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            char lab[300];
            snprintf(lab, sizeof lab, "%s%s", e->name, e->is_dir ? "/" : "");
            nk_bool on = !e->is_dir && strcmp(e->name, ui->file_name) == 0;
            if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on)) {
                if (e->is_dir) {
                    char next[PLAT_PATH_MAX];
                    if (plat_path_join(next, sizeof next, ui->file_dir, e->name)) {
                        snprintf(ui->file_dir, sizeof ui->file_dir, "%s", next);
                        list_dir(ui);
                    }
                    break;              /* the list just changed under us */
                }
                snprintf(ui->file_name, sizeof ui->file_name, "%s", e->name);
            }
        }
        if (!shown) {
            nk_layout_row_dynamic(c, S(ui, 22), 1);
            nk_label_colored(c, "No folders or projects here.", NK_TEXT_LEFT,
                             t->text_faint);
        }
        nk_group_end(c);
    }

    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 60));
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);
    nk_label_colored(c, "Name", NK_TEXT_LEFT, t->text_dim);
    ui_tip(ui, ui->file_mode == FILE_SAVE ? "The project file name; .pgp is added"
                                          : "The project to open");
    f = nk_edit_string_zero_terminated(c, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
                                       ui->file_name, sizeof ui->file_name,
                                       nk_filter_default);
    if (f & NK_EDIT_COMMITED)
        file_commit(ui);
}

/* ------------------------------------------------------------------ */
/* Confirm and About                                                   */
/* ------------------------------------------------------------------ */

static void confirm_body(PgUi *ui)
{
    char buf[256];
    snprintf(buf, sizeof buf, "\"%s\" has changes that are not saved. Save them "
             "first?", ui->model->project.title[0] ? ui->model->project.title
                                                    : "Untitled");
    ui_label_wrap(ui, buf, ui->theme->text);
}

static void about_body(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    nk_layout_row_dynamic(c, S(ui, 26), 1);
    nk_label_colored(c, PIPEGEN_NAME "  " PIPEGEN_VERSION, NK_TEXT_LEFT, ui->theme->accent);
    nk_layout_row_dynamic(c, S(ui, 20), 1);
    nk_label_colored(c, PIPEGEN_TAGLINE, NK_TEXT_LEFT, ui->theme->text);
    ui_gap(ui, 6);
    ui_label_wrap(ui, "Designs a two-stroke expansion chamber from the engine's "
                  "port timing and the speed it must work at, lets it be bent to "
                  "fit in 3D, and writes flat patterns for laser cutting as DXF "
                  "and PDF, rolled-and-welded or hydroformed. Cross-platform C "
                  "and SDL2.", ui->theme->text_dim);
    ui_gap(ui, 6);
    ui_section(ui, "This build");
    ui_info_row(ui, "Version", PIPEGEN_VERSION);
    ui_info_row(ui, "Built", __DATE__ " " __TIME__);
    SDL_version linked;
    SDL_GetVersion(&linked);
    ui_info_rowf(ui, "SDL", "%d.%d.%d at runtime, built against %d.%d.%d",
                 linked.major, linked.minor, linked.patch,
                 SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    ui_info_rowf(ui, "UI scale", "%.2f  (override with PIPEGEN_SCALE)", (double)ui->scale);
    ui_info_row(ui, "Graphics", ui->renderer);
    if (ui->glv)
        ui_info_row(ui, "", "PIPEGEN_RENDERER=software to draw without OpenGL");
    ui_gap(ui, 6);
    ui_section(ui, "Licence");
    ui_info_row(ui, "Licence", "GPL-3.0-or-later");
    ui_info_row(ui, "Copyright", "(C) 2026 Hugh Frater");
    ui_label_wrap(ui, "Free software with ABSOLUTELY NO WARRANTY; redistributable "
                  "under the GNU General Public License v3 or later — see "
                  "LICENSE. Nuklear, in third_party/, is MIT or public domain; "
                  "SDL2 is zlib-licensed.", ui->theme->text_faint);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

static const char *dialog_title(const PgUi *ui)
{
    switch (ui->dialog) {
    case DLG_WIZARD:  return "New project";
    case DLG_FILE:    return ui->file_mode == FILE_OPEN ? "Open project" : "Save project";
    case DLG_CONFIRM: return "Unsaved changes";
    case DLG_ABOUT:   return "About " PIPEGEN_NAME;
    default:          return "";
    }
}

static struct nk_vec2 dialog_size(PgDialog d)
{
    switch (d) {
    case DLG_WIZARD:  return nk_vec2(820, 720);
    case DLG_FILE:    return nk_vec2(640, 600);
    case DLG_CONFIRM: return nk_vec2(480, 200);
    case DLG_ABOUT:   return nk_vec2(620, 520);
    default:          return nk_vec2(500, 300);
    }
}

void ui_draw_dialog(PgUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    /* Nothing behind the scrim may answer the pointer. */
    ui->tip_text[0] = '\0';

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->scrim));
    if (nk_begin(c, "scrim", nk_rect(0, 0, (float)w, (float)h),
                 NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {}
    nk_end(c);
    nk_style_pop_style_item(c);

    struct nk_vec2 sz = dialog_size(ui->dialog);
    float dw = S(ui, sz.x), dh = S(ui, sz.y);
    bool autosize = ui->dialog == DLG_ABOUT || ui->dialog == DLG_CONFIRM;
    if (autosize && ui->dlg_measured == ui->dialog && ui->dlg_natural_h > 0.0f)
        dh = ui->dlg_natural_h;
    if (dw > w - S(ui, 40)) dw = w - S(ui, 40);
    if (dh > h - S(ui, 40)) dh = h - S(ui, 40);
    struct nk_rect r = nk_rect((w - dw) / 2, (h - dh) / 2, dw, dh);

    nk_style_push_float(c, &c->style.window.border, 1.0f);
    nk_style_push_color(c, &c->style.window.border_color, t->border);
    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->panel));

    /* Each dialog is its own window name, so one closing and another opening
     * in the same frame cannot inherit its position or scroll. */
    char name[32];
    snprintf(name, sizeof name, "dialog%d", (int)ui->dialog);
    nk_window_set_bounds(c, name, r);

    PgDialog showing = ui->dialog;
    if (nk_begin_titled(c, name, dialog_title(ui), r,
                        NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_CLOSABLE |
                        NK_WINDOW_NO_SCROLLBAR)) {
        nk_window_set_focus(c, name);

        float spacing = c->style.window.spacing.y;
        float pad_y = c->style.window.padding.y;
        struct nk_rect region = nk_window_get_content_region(c);
        /* An error wraps to as many lines as it needs, so a long one (the
         * Windows folder-protection advice) is read in full. */
        float err_h = ui->dlg_error[0]
            ? ui_wrap_height(ui, ui->dlg_error, region.w - c->style.window.padding.x * 2.0f
                                                - S(ui, 8)) + S(ui, 2)
            : 0.0f;
        float msg_h = ui->dlg_error[0] ? err_h + spacing : 0.0f;
        float below = S(ui, BTN_H) + spacing + msg_h + pad_y + S(ui, DLG_BOTTOM_MARGIN);
        float body_h = region.h - below;
        /* The floor keeps a squeezed file browser usable. An autosized dialog
         * is already as tall as its body, and a one-line body under the floor
         * would push the buttons out of the window. */
        float floor_h = autosize ? 1.0f : S(ui, 60);
        if (body_h < floor_h)
            body_h = floor_h;
        float chrome = nk_window_get_bounds(c).h - region.h;
        float used = 0.0f;

        nk_layout_row_dynamic(c, body_h, 1);
        if (nk_group_begin(c, "dlgbody", autosize ? NK_WINDOW_NO_SCROLLBAR : 0)) {
            struct nk_rect topb = nk_layout_widget_bounds(c);
            switch (showing) {
            case DLG_WIZARD:  wiz_body(ui);     break;
            case DLG_FILE:    file_body(ui);    break;
            case DLG_CONFIRM: confirm_body(ui); break;
            case DLG_ABOUT:   about_body(ui);   break;
            default: break;
            }
            nk_layout_row_dynamic(c, 0.0f, 1);
            used = nk_layout_widget_bounds(c).y - topb.y - c->style.window.spacing.y;
            nk_group_end(c);
        }
        /* From `below`, not region.h - body_h: after a clamp that difference
         * is short, and feeding it back shrank the dialog every frame until
         * the buttons were gone. */
        if (autosize && used > 0.0f) {
            ui->dlg_natural_h = chrome + below + used
                              + c->style.window.group_padding.y * 2.0f;
            ui->dlg_measured = showing;
        }

        if (ui->dlg_error[0]) {
            nk_layout_row_dynamic(c, err_h, 1);
            nk_label_colored_wrap(c, ui->dlg_error, t->alarm);
        }

        if (ui->dialog == showing) {
            DlgResult res;
            switch (showing) {
            case DLG_WIZARD:
                wiz_buttons(ui);
                break;
            case DLG_FILE:
                res = dialog_buttons(ui, ui->file_name[0] != '\0',
                                     ui->file_mode == FILE_OPEN ? "Open" : "Save", NULL);
                if (res == DLG_R_CANCEL) {
                    ui->dialog = DLG_NONE;
                    ui->pending = PENDING_NONE;
                    ui->save_then_pending = false;
                } else if (res == DLG_R_OK) {
                    file_commit(ui);
                }
                break;
            case DLG_CONFIRM:
                res = dialog_buttons(ui, true, "Save", "Discard");
                if (res == DLG_R_CANCEL) {
                    ui->dialog = DLG_NONE;
                    ui->pending = PENDING_NONE;
                } else if (res == DLG_R_ALT) {
                    ui->dialog = DLG_NONE;
                    ui->modified = false;
                    ui_continue_pending(ui);
                } else if (res == DLG_R_OK) {
                    ui->dialog = DLG_NONE;
                    if (ui->path[0]) {
                        if (ui_save_to(ui, ui->path))
                            ui_continue_pending(ui);
                    } else {
                        ui->save_then_pending = true;
                        ui_open_file_dialog(ui, FILE_SAVE);
                    }
                }
                break;
            case DLG_ABOUT:
                nk_layout_row_template_begin(c, S(ui, BTN_H));
                nk_layout_row_template_push_dynamic(c);
                nk_layout_row_template_push_static(c, S(ui, BTN_W));
                nk_layout_row_template_end(c);
                nk_skip(c);
                if (ui_primary_button(ui, "OK"))
                    ui->dialog = DLG_NONE;
                break;
            default:
                break;
            }
        }
    } else if (ui->dialog == showing) {
        ui->dialog = DLG_NONE;
        ui->pending = PENDING_NONE;
    }
    nk_end(c);

    nk_style_pop_style_item(c);
    nk_style_pop_color(c);
    nk_style_pop_float(c);

    /* The close box hides the window rather than closing it; a hidden window
     * would stay hidden the next time the same dialog opens. */
    if (ui->dialog != showing)
        nk_window_show(c, name, NK_SHOWN);
}
