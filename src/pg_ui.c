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
 * pg_ui.c — window shell, shared helpers, the project file and undo
 */
#define NK_IMPLEMENTATION
#define NK_SDL_RENDERER_IMPLEMENTATION
#include "nk.h"
#include "nuklear_sdl_renderer.h"
#include <math.h>
#include "pg_nkgl.h"

#include "pg_ui_int.h"
#include "pg_help.h"
#include "pg_version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define WIN_MIN_W 1180
#define WIN_MIN_H 700
#define WIN_MAX_FRAC 0.88f

/* An edit is recorded for undo once it has been still this long, so typing a
 * number or dragging a slider is one step, not a hundred. */
#define UNDO_SETTLE_MS 450

static const char *PAGE_NAME[PAGE_COUNT] = {
    "Design", "Patterns", "Profile", "Export", "Help"
};

static const char *PAGE_HINT[PAGE_COUNT] = {
    "The chamber in 3D: select pieces and joints, bend it to fit, edit "
    "every parameter",
    "Each flat pattern with its dimensions, and the sheet layouts",
    "The side profile to scale, and when the waves reach the port",
    "Write the DXF for the laser and the PDF for the workshop",
    "The manual, and what every control does (F1)"
};

/* ------------------------------------------------------------------ */
/* Tooltips                                                            */
/*                                                                     */
/* Every control that does something says what it does when the pointer */
/* rests on it. ui_tip() is called immediately before the widget,       */
/* because the bounds tested are the ones the next widget occupies.     */
/* Nuklear's own nk_tooltip() clips at the window edge and only answers */
/* for the focused window; this draws into the overlay buffer instead.  */
/* ------------------------------------------------------------------ */

#define TIP_DELAY_MS  700
#define TIP_MAX_W     340.0f
#define TIP_MAX_LINES 5

typedef struct { int off, len; } TipLine;

static float text_width(const PgUi *ui, const char *s, int len)
{
    const struct nk_user_font *f = ui->ctx->style.font;
    if (!f || len <= 0)
        return 0.0f;
    return f->width(f->userdata, f->height, s, len);
}

static int wrap_text(const PgUi *ui, const char *s, float w, TipLine *out,
                     int max_out)
{
    float space = text_width(ui, " ", 1);
    int lines = 0, start = 0, i = 0;
    float cur = 0.0f;

    while (s[i]) {
        int ws = i;
        while (s[i] && s[i] != ' ')
            i++;
        float word = text_width(ui, s + ws, i - ws);

        if (cur > 0.0f && cur + space + word > w) {
            if (out && lines < max_out) {
                out[lines].off = start;
                out[lines].len = ws - 1 - start;
            }
            lines++;
            start = ws;
            cur = word;
        } else {
            cur += (cur > 0.0f ? space : 0.0f) + word;
        }
        while (s[i] == ' ')
            i++;
    }
    if (out && lines < max_out) {
        out[lines].off = start;
        out[lines].len = i - start;
    }
    return lines + 1;
}

float ui_wrap_height(PgUi *ui, const char *text, float w)
{
    int n = wrap_text(ui, text, w, NULL, 0);
    return (float)n * (ui->ctx->style.font->height + S(ui, 4));
}

void ui_label_wrap(PgUi *ui, const char *text, struct nk_color col)
{
    struct nk_context *c = ui->ctx;
    struct nk_rect region = nk_window_get_content_region(c);
    float w = region.w - c->style.window.padding.x * 2.0f - S(ui, 8);
    if (w < S(ui, 60))
        w = S(ui, 60);
    nk_layout_row_dynamic(c, ui_wrap_height(ui, text, w) + S(ui, 2), 1);
    nk_label_colored_wrap(c, text, col);
}

void ui_tip(PgUi *ui, const char *text)
{
    struct nk_context *c = ui->ctx;
    if (!text || !text[0] || !c->current || !c->current->layout)
        return;
    if (c->current->popup.active)
        return;
    if (c->input.mouse.buttons[NK_BUTTON_LEFT].down)
        return;

    struct nk_rect b = nk_widget_bounds(c);
    struct nk_rect clip = c->current->layout->clip;
    if (!nk_input_is_mouse_hovering_rect(&c->input, b) ||
        !nk_input_is_mouse_hovering_rect(&c->input, clip))
        return;

    snprintf(ui->tip_text, sizeof ui->tip_text, "%s", text);
    ui->tip_over = b;
}

void ui_tip_rect(PgUi *ui, struct nk_rect over, const char *text)
{
    if (!text || !text[0])
        return;
    snprintf(ui->tip_text, sizeof ui->tip_text, "%s", text);
    ui->tip_over = over;
}

static void tip_draw(PgUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    if (!ui->tip_text[0]) {
        ui->tip_shown[0] = '\0';
        return;
    }
    if (strcmp(ui->tip_text, ui->tip_shown) != 0) {
        snprintf(ui->tip_shown, sizeof ui->tip_shown, "%s", ui->tip_text);
        ui->tip_since_ms = plat_now_ms();
        return;
    }
    if (plat_now_ms() - ui->tip_since_ms < TIP_DELAY_MS)
        return;

    TipLine ln[TIP_MAX_LINES];
    int n = wrap_text(ui, ui->tip_shown, S(ui, TIP_MAX_W), ln, TIP_MAX_LINES);
    if (n > TIP_MAX_LINES)
        n = TIP_MAX_LINES;

    float lh = c->style.font->height + S(ui, 3);
    float pad_x = S(ui, 9), pad_y = S(ui, 6);
    float tw = 0.0f;
    for (int i = 0; i < n; i++) {
        float lw = text_width(ui, ui->tip_shown + ln[i].off, ln[i].len);
        if (lw > tw) tw = lw;
    }

    struct nk_rect r;
    r.w = tw + pad_x * 2.0f;
    r.h = (float)n * lh + pad_y * 2.0f;
    r.x = ui->tip_over.x;
    r.y = ui->tip_over.y + ui->tip_over.h + S(ui, 6);
    if (r.y + r.h > (float)h - S(ui, 4))
        r.y = ui->tip_over.y - r.h - S(ui, 6);
    if (r.y < S(ui, 4)) r.y = S(ui, 4);
    if (r.x + r.w > (float)w - S(ui, 4)) r.x = (float)w - r.w - S(ui, 4);
    if (r.x < S(ui, 4)) r.x = S(ui, 4);

    struct nk_command_buffer *cb = &c->overlay;
    nk_command_buffer_init(cb, &c->memory, NK_CLIPPING_ON);
    nk_start_buffer(c, cb);
    nk_push_scissor(cb, nk_rect(0, 0, (float)w, (float)h));

    float rad = S(ui, 3);
    nk_fill_rect(cb, nk_rect(r.x + S(ui, 2), r.y + S(ui, 2), r.w, r.h), rad,
                 nk_rgba(0, 0, 0, t->is_light ? 36 : 90));
    nk_fill_rect(cb, r, rad, t->panel_alt);
    nk_stroke_rect(cb, r, rad, 1.0f, t->border);
    for (int i = 0; i < n; i++) {
        struct nk_rect lr = nk_rect(r.x + pad_x, r.y + pad_y + (float)i * lh, tw, lh);
        nk_draw_text(cb, lr, ui->tip_shown + ln[i].off, ln[i].len,
                     c->style.font, t->panel_alt, t->text);
    }
    nk_finish_buffer(c, cb);
}

/* ------------------------------------------------------------------ */
/* Form helpers                                                        */
/* ------------------------------------------------------------------ */

void ui_form_row(PgUi *ui, float h)
{
    nk_layout_row_template_begin(ui->ctx, S(ui, h));
    nk_layout_row_template_push_static(ui->ctx, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(ui->ctx);
    nk_layout_row_template_end(ui->ctx);
}

void ui_section(PgUi *ui, const char *title)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, 24.0f), 1);
    nk_label_colored(ui->ctx, title, NK_TEXT_LEFT, ui->theme->accent);
}

void ui_gap(PgUi *ui, float h)
{
    nk_layout_row_dynamic(ui->ctx, S(ui, h), 1);
    nk_skip(ui->ctx);
}

bool ui_primary_button(PgUi *ui, const char *label)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.button.normal, nk_style_item_color(t->accent));
    nk_style_push_style_item(c, &c->style.button.hover, nk_style_item_color(t->accent_hover));
    nk_style_push_style_item(c, &c->style.button.active, nk_style_item_color(t->accent_dim));
    nk_style_push_color(c, &c->style.button.text_normal, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.text_hover, t->text_on_accent);
    nk_style_push_color(c, &c->style.button.border_color, t->accent);

    bool hit = nk_button_label(c, label) != 0;

    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_color(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    nk_style_pop_style_item(c);
    return hit;
}

void ui_info_row(PgUi *ui, const char *k, const char *v)
{
    ui_form_row(ui, 22.0f);
    nk_label_colored(ui->ctx, k, NK_TEXT_LEFT, ui->theme->text_dim);
    nk_label(ui->ctx, v, NK_TEXT_LEFT);
}

void ui_info_rowf(PgUi *ui, const char *k, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    ui_info_row(ui, k, buf);
}

static void prop_row(PgUi *ui)
{
    nk_layout_row_template_begin(ui->ctx, S(ui, ROW_H));
    nk_layout_row_template_push_static(ui->ctx, S(ui, LABEL_W));
    nk_layout_row_template_push_dynamic(ui->ctx);
    nk_layout_row_template_push_static(ui->ctx, S(ui, UNIT_W));
    nk_layout_row_template_end(ui->ctx);
}

bool ui_prop(PgUi *ui, const char *label, const char *id, const char *tip,
             double *v, double min, double max, double step, const char *unit)
{
    struct nk_context *c = ui->ctx;
    prop_row(ui);
    nk_label_colored(c, label, NK_TEXT_LEFT, ui->theme->text_dim);
    /* A bare "#": Nuklear draws whatever follows the '#' inside the field,
     * and keys a '#' name by its order in the frame instead of its text. */
    (void)id;
    double before = *v;
    ui_tip(ui, tip);
    nk_property_double(c, "#", min, v, max, step, (float)(step / 4.0));
    nk_label_colored(c, unit ? unit : "", NK_TEXT_LEFT, ui->theme->text_faint);
    return *v != before;
}

bool ui_prop_int(PgUi *ui, const char *label, const char *id, const char *tip,
                 int *v, int min, int max, const char *unit)
{
    struct nk_context *c = ui->ctx;
    prop_row(ui);
    nk_label_colored(c, label, NK_TEXT_LEFT, ui->theme->text_dim);
    (void)id;
    int before = *v;
    ui_tip(ui, tip);
    nk_property_int(c, "#", min, v, max, 1, 0.05f);
    nk_label_colored(c, unit ? unit : "", NK_TEXT_LEFT, ui->theme->text_faint);
    return *v != before;
}

bool ui_check(PgUi *ui, const char *label, const char *tip, bool *v)
{
    ui_form_row(ui, ROW_H);
    nk_skip(ui->ctx);
    nk_bool b = *v;
    ui_tip(ui, tip);
    nk_checkbox_label(ui->ctx, label, &b);
    bool changed = (b != 0) != *v;
    *v = b != 0;
    return changed;
}

void ui_message(PgUi *ui, bool error, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ui->msg, sizeof ui->msg, fmt, ap);
    va_end(ap);
    ui->msg_error = error;
}

PgDrawCtx ui_draw_ctx(PgUi *ui, bool mouse_ok)
{
    struct nk_context *c = ui->ctx;
    PgDrawCtx dc;
    dc.cb = nk_window_get_canvas(c);
    dc.font = c->style.font;
    dc.theme = ui->theme;
    dc.scale = ui->scale;
    dc.mouse = (mouse_ok && ui->dialog == DLG_NONE)
             ? nk_vec2(c->input.mouse.pos.x, c->input.mouse.pos.y)
             : nk_vec2(-1, -1);
    return dc;
}

/* ------------------------------------------------------------------ */
/* Project, file and undo                                              */
/* ------------------------------------------------------------------ */

static void undo_reset(PgUi *ui)
{
    ui->undo[0] = ui->model->project;
    ui->undo_n = 1;
    ui->undo_pos = 0;
    ui->change_waiting = false;
}

void ui_set_project(PgUi *ui, const PgProject *p, const char *path)
{
    ui->model->project = *p;
    pg_project_sanitise(&ui->model->project);
    pg_model_update(ui->model);
    pg_project_write(&ui->model->project, ui->cur_text, TEXT_MAX);

    snprintf(ui->path, sizeof ui->path, "%s", path ? path : "");
    if (path && path[0])
        snprintf(ui->saved_text, TEXT_MAX, "%s", ui->cur_text);
    else
        ui->saved_text[0] = '\0';

    undo_reset(ui);
    ui->fit_pending = true;
    ui->view_dirty = true;
    ui->vopt.select_id = ui->vopt.hover_id = -1;
    ui->part_sel = 0;
    ui->sheet_sel = 0;
    pg_export_base_name(ui->model->project.title, ui->export_base,
                        sizeof ui->export_base);
    ui->export_msg[0] = '\0';
}

bool ui_open_path(PgUi *ui, const char *path)
{
    PgProject p;
    char err[256] = "";
    if (!pg_project_load(&p, path, err, sizeof err)) {
        ui_message(ui, true, "%s", err);
        return false;
    }
    ui_set_project(ui, &p, path);
    pg_settings_add_recent(&ui->settings, path);
    snprintf(ui->settings.last_project, sizeof ui->settings.last_project, "%s", path);
    char dir[PLAT_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    if (plat_path_parent(dir))
        snprintf(ui->settings.project_dir, sizeof ui->settings.project_dir, "%s", dir);
    pg_settings_save(&ui->settings);
    if (err[0])
        ui_message(ui, true, "%s", err);
    else
        ui_message(ui, false, "Opened %s", plat_path_leaf(path));
    return true;
}

bool ui_save_to(PgUi *ui, const char *path)
{
    char err[256];
    if (!pg_project_save(&ui->model->project, path, err, sizeof err)) {
        ui_message(ui, true, "%s", err);
        return false;
    }
    snprintf(ui->path, sizeof ui->path, "%s", path);
    pg_project_write(&ui->model->project, ui->saved_text, TEXT_MAX);
    pg_settings_add_recent(&ui->settings, path);
    snprintf(ui->settings.last_project, sizeof ui->settings.last_project, "%s", path);
    char dir[PLAT_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", path);
    if (plat_path_parent(dir))
        snprintf(ui->settings.project_dir, sizeof ui->settings.project_dir, "%s", dir);
    pg_settings_save(&ui->settings);
    ui_message(ui, false, "Saved %s", plat_path_leaf(path));
    return true;
}

bool ui_save(PgUi *ui, bool save_as)
{
    if (save_as || !ui->path[0]) {
        ui_open_file_dialog(ui, FILE_SAVE);
        return false;                      /* finishes when the dialog does */
    }
    return ui_save_to(ui, ui->path);
}

void ui_continue_pending(PgUi *ui)
{
    Pending p = ui->pending;
    ui->pending = PENDING_NONE;
    ui->save_then_pending = false;
    switch (p) {
    case PENDING_NEW:       ui_open_wizard(ui, NULL); break;
    case PENDING_OPEN:      ui_open_file_dialog(ui, FILE_OPEN); break;
    case PENDING_OPEN_PATH: ui_open_path(ui, ui->pending_path); break;
    case PENDING_QUIT:      ui->quit = true; break;
    default: break;
    }
}

void ui_request_new(PgUi *ui)
{
    if (ui->modified)
        ui_open_confirm(ui, PENDING_NEW);
    else
        ui_open_wizard(ui, NULL);
}

void ui_request_open(PgUi *ui)
{
    if (ui->modified)
        ui_open_confirm(ui, PENDING_OPEN);
    else
        ui_open_file_dialog(ui, FILE_OPEN);
}

void ui_request_open_path(PgUi *ui, const char *path)
{
    snprintf(ui->pending_path, sizeof ui->pending_path, "%s", path);
    if (ui->modified)
        ui_open_confirm(ui, PENDING_OPEN_PATH);
    else
        ui_open_path(ui, path);
}

bool ui_can_undo(const PgUi *ui) { return ui->undo_pos > 0 || ui->change_waiting; }
bool ui_can_redo(const PgUi *ui) { return ui->undo_pos < ui->undo_n - 1; }

static void undo_commit(PgUi *ui)
{
    if (!ui->change_waiting)
        return;
    ui->change_waiting = false;
    /* drop any redo history, then append; slide the window when full */
    ui->undo_n = ui->undo_pos + 1;
    if (ui->undo_n >= UNDO_MAX) {
        memmove(&ui->undo[0], &ui->undo[1], (UNDO_MAX - 1) * sizeof ui->undo[0]);
        ui->undo_n--;
        ui->undo_pos--;
    }
    ui->undo[ui->undo_n++] = ui->model->project;
    ui->undo_pos = ui->undo_n - 1;
}

static void restore(PgUi *ui, const PgProject *p)
{
    ui->model->project = *p;
    ui->view_dirty = true;
}

void ui_undo(PgUi *ui)
{
    undo_commit(ui);
    if (ui->undo_pos > 0) {
        ui->undo_pos--;
        restore(ui, &ui->undo[ui->undo_pos]);
        ui_message(ui, false, "Undone");
    }
}

void ui_redo(PgUi *ui)
{
    if (ui->undo_pos < ui->undo_n - 1) {
        ui->undo_pos++;
        restore(ui, &ui->undo[ui->undo_pos]);
        ui_message(ui, false, "Redone");
    }
}

/* Once a frame: rebuild the model if the project changed, and fold settled
 * edits into the undo history. */
static void sync_model(PgUi *ui)
{
    static char text[TEXT_MAX];
    pg_project_sanitise(&ui->model->project);
    pg_project_write(&ui->model->project, text, sizeof text);

    if (strcmp(text, ui->cur_text) != 0) {
        memcpy(ui->cur_text, text, TEXT_MAX);
        pg_model_update(ui->model);
        ui->view_dirty = true;

        bool at_snapshot = false;
        if (ui->undo_n > 0) {
            static char snap[TEXT_MAX];
            pg_project_write(&ui->undo[ui->undo_pos], snap, sizeof snap);
            at_snapshot = strcmp(snap, text) == 0;
        }
        if (!at_snapshot) {
            ui->change_waiting = true;
            ui->change_ms = plat_now_ms();
        } else {
            ui->change_waiting = false;
        }
    }

    bool mouse_down = ui->ctx->input.mouse.buttons[NK_BUTTON_LEFT].down;
    if (ui->change_waiting && !mouse_down &&
        plat_now_ms() - ui->change_ms > UNDO_SETTLE_MS)
        undo_commit(ui);

    ui->modified = strcmp(ui->cur_text, ui->saved_text) != 0;
}

/* ------------------------------------------------------------------ */
/* Status strip                                                        */
/* ------------------------------------------------------------------ */

static int warning_count(const PgModel *m)
{
    return m->design.n_warn + m->chain->n_warn + m->parts->n_warn +
           (m->layout->n_oversize ? 1 : 0);
}

static void draw_status(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    const PgModel *m = ui->model;
    const PgProject *p = &m->project;

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding, nk_vec2(S(ui, 14), S(ui, 8)));

    if (nk_begin(c, "status", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 30));
        nk_layout_row_template_push_dynamic(c);            /* project  */
        nk_layout_row_template_push_static(c, S(ui, 190)); /* engine   */
        nk_layout_row_template_push_static(c, S(ui, 250)); /* objective */
        nk_layout_row_template_push_static(c, S(ui, 150)); /* tuned    */
        nk_layout_row_template_push_static(c, S(ui, 170)); /* method   */
        nk_layout_row_template_push_static(c, S(ui, 150)); /* state    */
        nk_layout_row_template_end(c);

        char buf[200];
        snprintf(buf, sizeof buf, "%s%s", p->title[0] ? p->title : "Untitled",
                 ui->modified ? " *" : "");
        ui_tip(ui, ui->path[0] ? ui->path : "Not saved yet");
        nk_label(c, buf, NK_TEXT_LEFT);

        snprintf(buf, sizeof buf, "%s  \xc2\xb7  %.0f cc", p->engine.name,
                 m->design.displacement_cc);
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        if (p->tuning.objective == PG_OBJ_POWER_BAND)
            snprintf(buf, sizeof buf, "%s  %.0f\xe2\x80\x93%.0f rpm",
                     pg_objective_name(p->tuning.objective), p->tuning.rpm_lo,
                     p->tuning.rpm_hi);
        else
            snprintf(buf, sizeof buf, "%s  %.0f rpm",
                     pg_objective_name(p->tuning.objective), p->tuning.rpm);
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);

        snprintf(buf, sizeof buf, "tuned %.0f mm", m->design.tuned_len);
        ui_tip(ui, "Port face to the middle of the baffle: the length the "
                   "plugging pulse travels, out and back");
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->trace_a);

        nk_label_colored(c, pg_method_name(p->build.method), NK_TEXT_LEFT, t->text_dim);

        const PgChain *ch = m->chain;
        int clashes = ch->n_clash_box + ch->n_clash_engine + ch->n_clash_self +
                      ch->n_bad_mitre;
        int warns = warning_count(m);
        if (clashes) {
            snprintf(buf, sizeof buf, "\xe2\x97\x8f %d clash%s", clashes,
                     clashes == 1 ? "" : "es");
            ui_tip(ui, "A piece leaves the clearance box, hits the engine or "
                       "the chamber, or is too short for its mitres. See the "
                       "Design page");
            nk_label_colored(c, buf, NK_TEXT_RIGHT, t->alarm);
        } else if (warns) {
            snprintf(buf, sizeof buf, "%d to check", warns);
            ui_tip(ui, "Things worth checking. The Design page lists them "
                       "when nothing is selected");
            nk_label_colored(c, buf, NK_TEXT_RIGHT, t->warn);
        } else {
            nk_label_colored(c, "\xe2\x9c\x93 clear", NK_TEXT_RIGHT, t->ok);
        }
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Navigation rail                                                     */
/* ------------------------------------------------------------------ */

static void draw_rail(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding, nk_vec2(S(ui, 8), S(ui, 12)));

    if (nk_begin(c, "rail", r, NK_WINDOW_NO_SCROLLBAR)) {
        for (int i = 0; i < PAGE_COUNT; i++) {
            nk_layout_row_dynamic(c, S(ui, 32), 1);
            ui_tip(ui, PAGE_HINT[i]);
            nk_bool on = (ui->page == (PgPage)i);
            if (nk_selectable_label(c, PAGE_NAME[i], NK_TEXT_LEFT, &on) && on)
                ui->page = (PgPage)i;
        }

        ui_gap(ui, 10);

        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Start a new project with the wizard (Ctrl+N)");
        if (nk_button_label(c, "New..."))
            ui_request_new(ui);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Open a saved project (Ctrl+O)");
        if (nk_button_label(c, "Open..."))
            ui_request_open(ui);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, ui->path[0] ? "Save the project (Ctrl+S)"
                               : "Save the project: it has no file yet (Ctrl+S)");
        if (nk_button_label(c, "Save"))
            ui_save(ui, false);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Save under a new name (Ctrl+Shift+S)");
        if (nk_button_label(c, "Save as..."))
            ui_save(ui, true);

        ui_gap(ui, 8);
        nk_layout_row_dynamic(c, S(ui, 30), 2);
        bool cu = ui_can_undo(ui), cr = ui_can_redo(ui);
        if (!cu) nk_widget_disable_begin(c);
        ui_tip(ui, "Undo the last change (Ctrl+Z)");
        if (nk_button_label(c, "Undo") && cu)
            ui_undo(ui);
        if (!cu) nk_widget_disable_end(c);
        if (!cr) nk_widget_disable_begin(c);
        ui_tip(ui, "Redo (Ctrl+Y)");
        if (nk_button_label(c, "Redo") && cr)
            ui_redo(ui);
        if (!cr) nk_widget_disable_end(c);

        ui_gap(ui, 8);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Version, build and licence");
        if (nk_button_label(c, "About...")) {
            ui->dlg_error[0] = '\0';
            ui->dialog = DLG_ABOUT;
        }
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, ui->dark ? "Switch to the light theme" : "Switch to the dark theme");
        if (nk_button_label(c, ui->dark ? "Light theme" : "Dark theme"))
            ui->theme_toggle = true;
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Action bar                                                          */
/* ------------------------------------------------------------------ */

static void draw_action(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(t->rail));
    nk_style_push_vec2(c, &c->style.window.padding, nk_vec2(S(ui, 14), S(ui, 6)));

    if (nk_begin(c, "action", r, NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_template_begin(c, S(ui, 20));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_push_static(c, S(ui, 520));
        nk_layout_row_template_end(c);

        if (ui->msg[0]) {
            ui_tip(ui, ui->msg);           /* in full, when the bar cuts it */
            nk_label_colored(c, ui->msg, NK_TEXT_LEFT,
                             ui->msg_error ? t->alarm : t->text_dim);
        }
        else
            nk_label_colored(c, PIPEGEN_TAGLINE, NK_TEXT_LEFT, t->text_faint);

        char buf[PLAT_PATH_MAX + 32];
        if (ui->path[0])
            snprintf(buf, sizeof buf, "%s", ui->path);
        else
            snprintf(buf, sizeof buf, "not saved");
        nk_label_colored(c, buf, NK_TEXT_RIGHT, t->text_faint);
    }
    nk_end(c);

    nk_style_pop_vec2(c);
    nk_style_pop_style_item(c);
}

/* ------------------------------------------------------------------ */
/* Frame                                                               */
/* ------------------------------------------------------------------ */

static void update_title(PgUi *ui)
{
    char want[320];
    snprintf(want, sizeof want, "%s%s - %s %s",
             ui->model->project.title[0] ? ui->model->project.title : "Untitled",
             ui->modified ? " *" : "", PIPEGEN_NAME, PIPEGEN_VERSION);
    if (strcmp(want, ui->title) != 0) {
        snprintf(ui->title, sizeof ui->title, "%s", want);
        SDL_SetWindowTitle(ui->win, ui->title);
    }
}

void pg_ui_frame(PgUi *ui, int w, int h)
{
    struct nk_context *c = ui->ctx;

    if (ui->theme_toggle) {
        ui->theme_toggle = false;
        ui->dark = !ui->dark;
        ui->theme = ui->dark ? &PG_THEME_DARK : &PG_THEME_LIGHT;
        pg_theme_apply(c, ui->theme, ui->scale);
        ui->settings.dark = ui->dark;
        pg_settings_save(&ui->settings);
        ui->view_dirty = true;
    }

    sync_model(ui);
    design_tick(ui);
    update_title(ui);
    ui->tip_text[0] = '\0';

    float sh = S(ui, STATUS_H), ah = S(ui, ACTION_H), rw = S(ui, RAIL_W);
    float body_h = (float)h - sh - ah;
    if (body_h < 100.0f)
        body_h = 100.0f;

    draw_status(ui, nk_rect(0, 0, (float)w, sh));
    draw_rail(ui, nk_rect(0, sh, rw, body_h));
    draw_action(ui, nk_rect(0, (float)h - ah, (float)w, ah));

    struct nk_rect content = nk_rect(rw, sh, (float)w - rw, body_h);

    nk_style_push_style_item(c, &c->style.window.fixed_background,
                             nk_style_item_color(ui->theme->bg));
    bool scrolls = (ui->page == PAGE_EXPORT || ui->page == PAGE_HELP);
    if (nk_begin(c, "content", content, scrolls ? 0 : NK_WINDOW_NO_SCROLLBAR)) {
        if (ui->page != ui->last_page) {
            nk_window_set_scroll(c, 0, 0);
            ui->last_page = ui->page;
        }
        struct nk_rect inner = nk_window_get_content_region(c);
        switch (ui->page) {
        case PAGE_DESIGN:   page_design(ui, inner);   break;
        case PAGE_PATTERNS: page_patterns(ui, inner); break;
        case PAGE_PROFILE:  page_profile(ui, inner);  break;
        case PAGE_EXPORT:   page_export(ui, inner);   break;
        case PAGE_HELP:     page_help(ui, inner);     break;
        default: break;
        }
    }
    nk_end(c);
    nk_style_pop_style_item(c);

    if (ui->dialog != DLG_NONE)
        ui_draw_dialog(ui, w, h);

    tip_draw(ui, w, h);
}

/* ------------------------------------------------------------------ */
/* Lifetime                                                            */
/* ------------------------------------------------------------------ */

void pg_ui_output_size(const PgUi *ui, int *w, int *h)
{
    *w = *h = 0;
    if (ui->ren)
        SDL_GetRendererOutputSize(ui->ren, w, h);
    else
        SDL_GL_GetDrawableSize(ui->win, w, h);
}

static float detect_scale(const PgUi *ui)
{
    const char *env = getenv("PIPEGEN_SCALE");
    if (env && atof(env) > 0.1)
        return (float)atof(env);

    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(ui->win, &ww, &wh);
    pg_ui_output_size(ui, &dw, &dh);
    float scale = (ww > 0) ? (float)dw / (float)ww : 1.0f;

    float ddpi = 0;
    if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(ui->win), &ddpi, NULL, NULL) == 0) {
        float dpi_scale = ddpi / 96.0f;
        if (dpi_scale > scale)
            scale = dpi_scale;
    }
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

static void load_font(PgUi *ui)
{
    struct nk_font_atlas *atlas = NULL;
    nk_sdl_font_stash_begin(&atlas);

    static const nk_rune ranges[] = {
        0x0020, 0x00FF, 0x2010, 0x2027, 0x2190, 0x2193, 0x2212, 0x2212,
        0x25A0, 0x25FF, 0x2713, 0x2713, 0
    };
    struct nk_font_config cfg = nk_font_config(14.0f * ui->scale);
    cfg.range = ranges;
    cfg.oversample_h = 2;
    cfg.oversample_v = 1;
    cfg.pixel_snap = 0;

    struct nk_font *font = NULL;

    /* A bundled face first, relative to the executable, so the interface
     * reads the same on a machine with no fonts — what an AppImage target
     * looks like. SDL_GetBasePath because an AppImage mounts somewhere new
     * every run. */
    char bundled[2][512];
    bundled[0][0] = bundled[1][0] = '\0';
    char *base = SDL_GetBasePath();
    if (base) {
        snprintf(bundled[0], sizeof bundled[0], "%sDejaVuSans.ttf", base);
        snprintf(bundled[1], sizeof bundled[1], "%s../share/pipegen/DejaVuSans.ttf", base);
        SDL_free(base);
    }
    const char *candidates[] = {
        bundled[0], bundled[1],
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/Library/Fonts/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        NULL
    };
    for (int i = 0; candidates[i] && !font; i++) {
        if (!candidates[i][0] || !plat_file_exists(candidates[i]))
            continue;
        font = nk_font_atlas_add_from_file(atlas, candidates[i], 14.0f * ui->scale, &cfg);
    }
    if (ui->ren)
        nk_sdl_font_stash_end();
    else
        pg_nkgl_font_stash_end();
    if (font)
        nk_style_set_font(ui->ctx, &font->handle);
}

PgUi *pg_ui_create(const PgUiVideo *video, const PgUiStart *start)
{
    PgUi *ui = calloc(1, sizeof *ui);
    if (!ui) {
        pg_glview_free(video->glview);
        return NULL;
    }
    ui->win = video->win;
    ui->ren = video->ren;
    ui->glv = video->glview;
    snprintf(ui->renderer, sizeof ui->renderer, "%s",
             video->renderer ? video->renderer : "");
    ui->saved_text = calloc(1, TEXT_MAX);
    ui->cur_text = calloc(1, TEXT_MAX);
    ui->undo = calloc(UNDO_MAX, sizeof *ui->undo);
    ui->entries = calloc(FILE_ENTRIES, sizeof *ui->entries);
    ui->fold_text = calloc(1, TEXT_MAX);
    ui->model = pg_model_new();
    ui->wiz_model = pg_model_new();
    if (!ui->saved_text || !ui->cur_text || !ui->undo || !ui->entries ||
        !ui->fold_text || !ui->model || !ui->wiz_model) {
        pg_ui_destroy(ui);
        return NULL;
    }

    pg_settings_load(&ui->settings);
    ui->dark = ui->settings.dark;
    ui->theme = ui->dark ? &PG_THEME_DARK : &PG_THEME_LIGHT;
    ui->scale = detect_scale(ui);
    ui->page = PAGE_DESIGN;
    ui->last_page = PAGE_COUNT;

    if (!ui->ren) {
        char err[512];
        if (!pg_nkgl_init(err, sizeof err)) {
            fprintf(stderr, "pipegen: OpenGL interface: %s\n", err);
            pg_ui_destroy(ui);
            return NULL;
        }
    }
    ui->ctx = nk_sdl_init(ui->win, ui->ren);
    if (!ui->ctx) {
        pg_ui_destroy(ui);
        return NULL;
    }
    load_font(ui);
    pg_theme_apply(ui->ctx, ui->theme, ui->scale);

    design_init(ui);

    PgProject def;
    pg_project_default(&def);
    ui_set_project(ui, &def, NULL);

    const char *open = start ? start->project : NULL;
    if (!open && !(start && start->wizard) && ui->settings.last_project[0] &&
        plat_file_exists(ui->settings.last_project))
        open = ui->settings.last_project;

    if (open)
        ui_open_path(ui, open);
    if ((start && start->wizard) || !open)
        ui_open_wizard(ui, NULL);
    return ui;
}

void pg_ui_fit_window(PgUi *ui, int base_w, int base_h)
{
    if (!ui)
        return;
    int ww = 0, wh = 0, dw = 0, dh = 0;
    SDL_GetWindowSize(ui->win, &ww, &wh);
    pg_ui_output_size(ui, &dw, &dh);
    float px_per_unit = (ww > 0 && dw > 0) ? (float)dw / (float)ww : 1.0f;

    float unit_scale = ui->scale / px_per_unit;
    float w = (float)base_w * unit_scale, h = (float)base_h * unit_scale;

    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetWindowDisplayIndex(ui->win), &usable) == 0 &&
        usable.w > 0 && usable.h > 0) {
        float max_w = (float)usable.w * WIN_MAX_FRAC, max_h = (float)usable.h * WIN_MAX_FRAC;
        float shrink = 1.0f;
        if (w > max_w) shrink = max_w / w;
        if (h > max_h && max_h / h < shrink) shrink = max_h / h;
        w *= shrink;
        h *= shrink;
    }
    int min_w = (int)((float)WIN_MIN_W * unit_scale);
    int min_h = (int)((float)WIN_MIN_H * unit_scale);
    SDL_SetWindowMinimumSize(ui->win, min_w, min_h);
    if (w < (float)min_w) w = (float)min_w;
    if (h < (float)min_h) h = (float)min_h;
    SDL_SetWindowSize(ui->win, (int)w, (int)h);
    SDL_SetWindowPosition(ui->win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void pg_ui_destroy(PgUi *ui)
{
    if (!ui)
        return;
    if (ui->ctx) {
        pg_settings_save(&ui->settings);
        design_shutdown(ui);
        if (ui->ren)
            nk_sdl_shutdown();
        else
            pg_nkgl_shutdown();
    }
    pg_glview_free(ui->glv);
    pg_model_free(ui->model);
    pg_model_free(ui->wiz_model);
    free(ui->saved_text);
    free(ui->cur_text);
    free(ui->undo);
    free(ui->entries);
    pg_fold_end(ui->folder);
    free(ui->fold_text);
    free(ui);
}

void pg_ui_input_begin(PgUi *ui) { nk_input_begin(ui->ctx); }
void pg_ui_input_end(PgUi *ui)   { nk_input_end(ui->ctx); }

/* True while a text or number field has the keyboard. Not
 * nk_item_is_any_active(): that is also true whenever the pointer is over
 * any window, which here is always, and it silently ate every shortcut. */
static bool typing(const struct nk_context *ctx)
{
    for (const struct nk_window *w = ctx->begin; w; w = w->next)
        if (!(w->flags & NK_WINDOW_HIDDEN) && (w->edit.active || w->property.active))
            return true;
    return false;
}

bool pg_ui_handle_event(PgUi *ui, SDL_Event *e)
{
    if (e->type == SDL_QUIT) {
        if (ui->modified && ui->dialog == DLG_NONE)
            ui_open_confirm(ui, PENDING_QUIT);
        else if (!ui->modified)
            ui->quit = true;
        return true;
    }
    if (e->type == SDL_KEYDOWN) {
        SDL_Keycode k = e->key.keysym.sym;
        Uint16 mod = e->key.keysym.mod;
        bool ctrl = (mod & KMOD_CTRL) != 0;

        if (k == SDLK_ESCAPE && ui->dialog != DLG_NONE && ui->dialog != DLG_CONFIRM) {
            ui->dialog = DLG_NONE;
            return true;
        }
        if (k == SDLK_F1) {
            ui->dialog = DLG_NONE;
            ui->page = PAGE_HELP;
            return true;
        }
        if (ui->dialog == DLG_NONE && ctrl) {
            /* Text fields keep Ctrl+Z for themselves while they have the
             * keyboard; everywhere else it is the project's undo. */
            bool editing = typing(ui->ctx);
            switch (k) {
            case SDLK_z:
                if (editing) break;
                if (mod & KMOD_SHIFT) ui_redo(ui); else ui_undo(ui);
                return true;
            case SDLK_y:
                if (editing) break;
                ui_redo(ui);
                return true;
            case SDLK_s: ui_save(ui, (mod & KMOD_SHIFT) != 0); return true;
            case SDLK_o: ui_request_open(ui); return true;
            case SDLK_n: ui_request_new(ui); return true;
            default: break;
            }
        }
        if (ui->dialog == DLG_NONE && ui->page == PAGE_DESIGN &&
            !typing(ui->ctx) && design_handle_key(ui, k, mod))
            return true;
    }
    return nk_sdl_handle_event(e) != 0;
}

void pg_ui_present(PgUi *ui)
{
    const struct nk_color bg = ui->theme->bg;
    if (ui->ren) {
        SDL_SetRenderDrawColor(ui->ren, bg.r, bg.g, bg.b, 255);
        SDL_RenderClear(ui->ren);
        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_RenderPresent(ui->ren);
        return;
    }
    int w, h;
    pg_ui_output_size(ui, &w, &h);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, w, h);
    gl.Disable(GL_SCISSOR_TEST);
    gl.ClearColor(bg.r / 255.0f, bg.g / 255.0f, bg.b / 255.0f, 1.0f);
    gl.Clear(GL_COLOR_BUFFER_BIT);
    pg_nkgl_render(w, h, NK_ANTI_ALIASING_ON);
    SDL_GL_SwapWindow(ui->win);
}

bool pg_ui_quit_requested(const PgUi *ui) { return ui->quit; }

bool pg_ui_busy(const PgUi *ui) { return ui->folder != NULL; }
