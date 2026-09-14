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
 * pg_ui_design.c — the Design page: the 3D view, and the inspector beside it
 *
 * The view is rendered by pg_view3d into a software raster, uploaded to a
 * streaming texture, and drawn as an image inside the page's Nuklear window —
 * so dialogs and tooltips stay on top of it with no special handling. It is
 * re-rendered only when something it shows has changed.
 *
 * The inspector's forms edit the model's project directly. The shell notices
 * the change at the top of the next frame, rebuilds the model and records
 * the edit for undo.
 */
#include "pg_ui_int.h"
#include "pg_vec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DEG (M_PI / 180.0)

/* ------------------------------------------------------------------ */
/* View lifetime and rendering                                         */
/* ------------------------------------------------------------------ */

void design_init(PgUi *ui)
{
    pg_camera_default(&ui->cam);
    ui->vopt = (PgViewOpts){
        .colour = PG_COLOUR_SECTION, .show_engine = true, .show_box = true,
        .show_seams = true, .show_grid = true,
        .hover_id = -1, .select_id = -1, .highlight_section = -1,
    };
    ui->raster = pg_raster_new(64, 64);
    ui->ss = 1;
    ui->fit_pending = true;
    ui->view_dirty = true;
    ui->insp = INSP_SELECT;
    ui->drag_joint = -1;
    pg_fold_defaults(&ui->fold, NULL);
}

void design_shutdown(PgUi *ui)
{
    if (ui->tex)
        SDL_DestroyTexture(ui->tex);
    ui->tex = NULL;
    pg_raster_free(ui->raster);
    ui->raster = NULL;
    free(ui->tex_pixels);
    ui->tex_pixels = NULL;
}

static void render_view(PgUi *ui, int w, int h)
{
    if (w < 8 || h < 8 || !ui->raster)
        return;

    /* Supersample small views for smooth edges; a view this large on a
     * high-density screen has pixels too fine to need it. */
    int ss = ((long)w * (long)h <= 1400000L) ? 2 : 1;

    if (!ui->tex || ui->tex_w != w || ui->tex_h != h || ui->ss != ss) {
        if (ui->tex)
            SDL_DestroyTexture(ui->tex);
        ui->tex = SDL_CreateTexture(ui->ren, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING, w, h);
        free(ui->tex_pixels);
        ui->tex_pixels = malloc((size_t)w * (size_t)h * 4);
        if (!ui->tex || !ui->tex_pixels || !pg_raster_resize(ui->raster, w * ss, h * ss)) {
            ui_message(ui, true, "Not enough memory for the 3D view at this size.");
            if (ui->tex)
                SDL_DestroyTexture(ui->tex);
            ui->tex = NULL;
            return;
        }
        ui->tex_w = w;
        ui->tex_h = h;
        ui->ss = ss;
        ui->view_dirty = true;
    }

    if (ui->fit_pending) {
        pg_camera_fit(&ui->cam, ui->model->chain, ui->vopt.show_engine,
                      (double)w / (double)h);
        ui->fit_pending = false;
        ui->view_dirty = true;
    }

    if (!ui->view_dirty && ui->view_gen == ui->model->generation)
        return;

    pg_view3d_render(ui->raster, &ui->cam, &ui->model->project, ui->model->chain,
                     &ui->vopt, ui->dark ? &PG_VIEW_DARK : &PG_VIEW_LIGHT);
    pg_raster_downsample(ui->raster, ss, ui->tex_pixels, w, h);
    SDL_UpdateTexture(ui->tex, NULL, ui->tex_pixels, w * 4);
    ui->view_dirty = false;
    ui->view_gen = ui->model->generation;
}

/* ------------------------------------------------------------------ */
/* Selection and bending                                               */
/* ------------------------------------------------------------------ */

static void validate_selection(PgUi *ui)
{
    const PgChain *c = ui->model->chain;
    for (int k = 0; k < 2; k++) {
        int *id = k ? &ui->vopt.select_id : &ui->vopt.hover_id;
        if (PG_PICK_IS_PIECE(*id) && *id >= c->n_pieces)
            *id = -1;
        if (PG_PICK_IS_JOINT(*id) && *id - PG_PICK_JOINT >= c->n_joints)
            *id = -1;
    }
}

static void select_id(PgUi *ui, int id)
{
    if (id == PG_PICK_ENGINE)
        id = -1;
    if (ui->vopt.select_id != id) {
        ui->vopt.select_id = id;
        ui->view_dirty = true;
    }
    ui->insp = INSP_SELECT;
}

static double joint_snap(const PgChain *c, int j)
{
    const PgJoint *jt = &c->joint[j];
    return fmin(c->piece[jt->before].len, c->piece[jt->after].len) / 2.0;
}

static void set_joint(PgUi *ui, int j, double bend, double roll)
{
    const PgChain *c = ui->model->chain;
    if (j < 0 || j >= c->n_joints)
        return;
    bend = fmax(0.0, fmin(90.0, bend));
    roll = fmod(roll, 360.0);
    if (roll < 0.0)
        roll += 360.0;
    if (!pg_route_set_bend(&ui->model->project.route, c->joint[j].x,
                           joint_snap(c, j), bend, roll))
        ui_message(ui, true, "At most %d bends.", PG_MAX_BENDS);
}

static void select_joint_step(PgUi *ui, int step)
{
    const PgChain *c = ui->model->chain;
    if (c->n_joints == 0)
        return;
    int j;
    if (PG_PICK_IS_JOINT(ui->vopt.select_id))
        j = ui->vopt.select_id - PG_PICK_JOINT + step;
    else if (PG_PICK_IS_PIECE(ui->vopt.select_id))
        j = ui->vopt.select_id - (step < 0 ? 1 : 0);
    else
        j = step > 0 ? 0 : c->n_joints - 1;
    if (j < 0) j = 0;
    if (j >= c->n_joints) j = c->n_joints - 1;
    select_id(ui, PG_PICK_JOINT + j);
}

bool design_handle_key(PgUi *ui, SDL_Keycode key, Uint16 mod)
{
    const PgChain *c = ui->model->chain;
    int sel = ui->vopt.select_id;
    bool joint = PG_PICK_IS_JOINT(sel);
    int j = joint ? sel - PG_PICK_JOINT : -1;
    double bend = joint ? c->joint[j].bend_deg : 0.0;
    double roll = joint ? c->joint[j].roll_deg : 0.0;
    (void)mod;

    switch (key) {
    case SDLK_ESCAPE: select_id(ui, -1); return true;
    case SDLK_f:      ui->fit_pending = true; return true;
    case SDLK_1: pg_camera_preset(&ui->cam, PG_VIEW_3Q);   ui->fit_pending = true; return true;
    case SDLK_2: pg_camera_preset(&ui->cam, PG_VIEW_SIDE); ui->fit_pending = true; return true;
    case SDLK_3: pg_camera_preset(&ui->cam, PG_VIEW_TOP);  ui->fit_pending = true; return true;
    case SDLK_4: pg_camera_preset(&ui->cam, PG_VIEW_END);  ui->fit_pending = true; return true;
    case SDLK_PAGEUP:   select_joint_step(ui, -1); return true;
    case SDLK_PAGEDOWN: select_joint_step(ui, +1); return true;
    default: break;
    }
    if (!joint)
        return false;
    switch (key) {
    case SDLK_LEFTBRACKET:  set_joint(ui, j, bend - 5.0, roll); return true;
    case SDLK_RIGHTBRACKET: set_joint(ui, j, bend + 5.0, roll); return true;
    case SDLK_COMMA:        set_joint(ui, j, bend, roll - 15.0); return true;
    case SDLK_PERIOD:       set_joint(ui, j, bend, roll + 15.0); return true;
    case SDLK_DELETE:
    case SDLK_BACKSPACE:    set_joint(ui, j, 0.0, 0.0); return true;
    default: return false;
    }
}

/* ------------------------------------------------------------------ */
/* Viewport interaction                                                */
/* ------------------------------------------------------------------ */

static void view_input(PgUi *ui, struct nk_rect vr)
{
    struct nk_context *c = ui->ctx;
    struct nk_input *in = &c->input;
    const PgChain *ch = ui->model->chain;

    bool can = ui->dialog == DLG_NONE;
    bool over = can && nk_input_is_mouse_hovering_rect(in, vr);
    float mx = in->mouse.pos.x, my = in->mouse.pos.y;
    /* From SDL, not Nuklear: the SDL backend never reports a Ctrl key held
     * on its own, so Ctrl+drag orbited instead of bending. */
    SDL_Keymod mods = SDL_GetModState();
    bool ctrl = (mods & KMOD_CTRL) != 0;
    bool shift = (mods & KMOD_SHIFT) != 0;

    /* what is under the pointer */
    if (ui->drag == DRAG_NONE) {
        int id = -1;
        if (over)
            id = pg_raster_id_at(ui->raster, (int)((mx - vr.x) * ui->ss),
                                 (int)((my - vr.y) * ui->ss));
        if (id == PG_PICK_ENGINE)
            id = -1;
        if (id != ui->vopt.hover_id) {
            ui->vopt.hover_id = id;
            ui->view_dirty = true;
        }
    }

    const struct nk_mouse_button *L = &in->mouse.buttons[NK_BUTTON_LEFT];
    const struct nk_mouse_button *R = &in->mouse.buttons[NK_BUTTON_RIGHT];
    const struct nk_mouse_button *M = &in->mouse.buttons[NK_BUTTON_MIDDLE];

    if (over && ui->drag == DRAG_NONE) {
        if (L->clicked && L->down) {
            ui->drag_from = nk_vec2(mx, my);
            if (ctrl && PG_PICK_IS_JOINT(ui->vopt.select_id)) {
                ui->drag = DRAG_BEND;
                ui->drag_joint = ui->vopt.select_id - PG_PICK_JOINT;
                ui->drag_bend = ch->joint[ui->drag_joint].bend_deg;
                ui->drag_roll = ch->joint[ui->drag_joint].roll_deg;
            } else {
                ui->drag = shift ? DRAG_PAN : DRAG_PENDING;
            }
        } else if ((R->clicked && R->down) || (M->clicked && M->down)) {
            ui->drag = DRAG_PAN;
        }
    }

    if (ui->drag != DRAG_NONE) {
        struct nk_vec2 d = in->mouse.delta;
        bool down = L->down || R->down || M->down;

        if (ui->drag == DRAG_PENDING &&
            (fabs(mx - ui->drag_from.x) > 3.0f || fabs(my - ui->drag_from.y) > 3.0f))
            ui->drag = DRAG_ORBIT;

        switch (ui->drag) {
        case DRAG_ORBIT:
            if (d.x != 0.0f || d.y != 0.0f) {
                pg_camera_orbit(&ui->cam, d.x / ui->scale, d.y / ui->scale);
                ui->view_dirty = true;
            }
            break;
        case DRAG_PAN:
            if (d.x != 0.0f || d.y != 0.0f) {
                pg_camera_pan(&ui->cam, d.x, d.y, (int)vr.h);
                ui->view_dirty = true;
            }
            break;
        case DRAG_BEND: {
            double dy = (my - ui->drag_from.y) / ui->scale;
            double dx = (mx - ui->drag_from.x) / ui->scale;
            double bend = ui->drag_bend - dy * 0.3;
            double roll = ui->drag_roll + dx * 0.6;
            /* hold Shift as well to step in 5 and 15 degree increments */
            if (shift) {
                bend = round(bend / 5.0) * 5.0;
                roll = round(roll / 15.0) * 15.0;
            }
            set_joint(ui, ui->drag_joint, bend, roll);
            break;
        }
        default:
            break;
        }

        if (!down) {
            if (ui->drag == DRAG_PENDING)
                select_id(ui, ui->vopt.hover_id);
            ui->drag = DRAG_NONE;
        }
    }

    if (over && in->mouse.scroll_delta.y != 0.0f) {
        pg_camera_zoom_at(&ui->cam, pow(0.87, in->mouse.scroll_delta.y),
                          mx - vr.x, my - vr.y, (int)vr.w, (int)vr.h);
        ui->view_dirty = true;
    }
    if (over && in->mouse.buttons[NK_BUTTON_DOUBLE].clicked &&
        in->mouse.buttons[NK_BUTTON_DOUBLE].down)
        ui->fit_pending = true;
}

/* ------------------------------------------------------------------ */
/* Overlays on the view                                                */
/* ------------------------------------------------------------------ */

static void overlay_text(PgUi *ui, struct nk_command_buffer *cb, float x, float y,
                         const char *s, struct nk_color col, bool right)
{
    const struct nk_user_font *f = ui->ctx->style.font;
    float w = f->width(f->userdata, f->height, s, (int)strlen(s));
    if (right)
        x -= w;
    struct nk_rect r = nk_rect(x - S(ui, 6), y - S(ui, 3), w + S(ui, 12),
                               f->height + S(ui, 6));
    struct nk_color bg = ui->theme->panel;
    bg.a = 200;
    nk_fill_rect(cb, r, S(ui, 3), bg);
    nk_draw_text(cb, nk_rect(x, y, w + 2, f->height), s, (int)strlen(s), f,
                 nk_rgba(0, 0, 0, 0), col);
}

static void describe(PgUi *ui, int id, char *buf, size_t cap)
{
    const PgModel *m = ui->model;
    const PgChain *c = m->chain;
    buf[0] = '\0';
    if (PG_PICK_IS_PIECE(id) && id < c->n_pieces) {
        const PgPiece *pc = &c->piece[id];
        const char *part = pc->part >= 0 ? m->parts->part[pc->part].id : "";
        snprintf(buf, cap, "%s%s%s  \xc2\xb7  \xc3\x98%.1f to %.1f  \xc2\xb7  %.0f long",
                 part, part[0] ? "  " : "", pc->name, pc->d0, pc->d1, pc->len);
    } else if (PG_PICK_IS_JOINT(id) && id - PG_PICK_JOINT < c->n_joints) {
        const PgJoint *jt = &c->joint[id - PG_PICK_JOINT];
        if (jt->bend_deg > 1e-6)
            snprintf(buf, cap, "Joint %d at %.0f mm  \xc2\xb7  bent %.1f\xc2\xb0, "
                     "roll %.0f\xc2\xb0", id - PG_PICK_JOINT + 1, jt->x,
                     jt->bend_deg, jt->roll_deg);
        else
            snprintf(buf, cap, "Joint %d at %.0f mm  \xc2\xb7  straight  \xc2\xb7  "
                     "click, then Ctrl+drag to bend", id - PG_PICK_JOINT + 1, jt->x);
    }
}

static void draw_triad(PgUi *ui, struct nk_command_buffer *cb, struct nk_rect vr)
{
    const PgTheme *t = ui->theme;
    float cx = vr.x + vr.w - S(ui, 44), cy = vr.y + vr.h - S(ui, 44);
    double o[3], sx0, sy0;
    v3_copy(o, ui->cam.target);
    if (!pg_camera_project(&ui->cam, (int)vr.w, (int)vr.h, o, &sx0, &sy0))
        return;
    static const char *name[3] = { "X", "Y", "Z" };
    struct nk_color col[3] = { t->alarm, t->ok, t->trace_b };
    for (int i = 0; i < 3; i++) {
        double p[3], sx, sy;
        v3_copy(p, o);
        p[i] += ui->cam.dist * 0.05;
        if (!pg_camera_project(&ui->cam, (int)vr.w, (int)vr.h, p, &sx, &sy))
            continue;
        double dx = sx - sx0, dy = sy - sy0, l = hypot(dx, dy);
        if (l < 1e-6)
            continue;
        float ex = cx + (float)(dx / l) * S(ui, 26), ey = cy + (float)(dy / l) * S(ui, 26);
        nk_stroke_line(cb, cx, cy, ex, ey, S(ui, 2), col[i]);
        const struct nk_user_font *f = ui->ctx->style.font;
        nk_draw_text(cb, nk_rect(ex + (float)(dx / l) * S(ui, 4) - S(ui, 4),
                                 ey + (float)(dy / l) * S(ui, 4) - f->height / 2,
                                 S(ui, 12), f->height),
                     name[i], 1, f, nk_rgba(0, 0, 0, 0), col[i]);
    }
}

static void draw_overlays(PgUi *ui, struct nk_rect vr)
{
    struct nk_command_buffer *cb = nk_window_get_canvas(ui->ctx);
    const PgTheme *t = ui->theme;
    const PgModel *m = ui->model;
    const PgChain *c = m->chain;
    char buf[256];

    float pad = S(ui, 12);
    int id = ui->vopt.hover_id >= 0 ? ui->vopt.hover_id : ui->vopt.select_id;
    describe(ui, id, buf, sizeof buf);
    if (buf[0])
        overlay_text(ui, cb, vr.x + pad, vr.y + pad, buf,
                     id == ui->vopt.select_id ? t->text : t->text_dim, false);

    int clashes = c->n_clash_box + c->n_clash_engine + c->n_clash_self + c->n_bad_mitre;
    if (clashes) {
        snprintf(buf, sizeof buf, "%s%s%s%s",
                 c->n_clash_box ? "leaves the box  " : "",
                 c->n_clash_engine ? "hits the engine  " : "",
                 c->n_clash_self ? "hits itself  " : "",
                 c->n_bad_mitre ? "mitres cross" : "");
        overlay_text(ui, cb, vr.x + vr.w - pad, vr.y + pad, buf, t->alarm, true);
    }

    double ex = c->bmax[0] - c->bmin[0], ey = c->bmax[1] - c->bmin[1],
           ez = c->bmax[2] - c->bmin[2];
    snprintf(buf, sizeof buf,
             "%.2f m along the centreline  \xc2\xb7  envelope %.0f \xc3\x97 %.0f "
             "\xc3\x97 %.0f mm  \xc2\xb7  on time %.0f\xe2\x80\x93%.0f rpm",
             m->design.total_len / 1000.0, ex, ey, ez, m->design.band_lo,
             m->design.band_hi);
    overlay_text(ui, cb, vr.x + pad, vr.y + vr.h - pad - ui->ctx->style.font->height,
                 buf, t->text_dim, false);

    draw_triad(ui, cb, vr);
}

/* ------------------------------------------------------------------ */
/* Forms — shared with the wizard                                      */
/* ------------------------------------------------------------------ */

static bool edit_text(PgUi *ui, const char *label, const char *tip, char *buf,
                      int cap)
{
    char before[512];
    snprintf(before, sizeof before, "%s", buf);
    ui_form_row(ui, ROW_H);
    nk_label_colored(ui->ctx, label, NK_TEXT_LEFT, ui->theme->text_dim);
    ui_tip(ui, tip);
    nk_edit_string_zero_terminated(ui->ctx, NK_EDIT_FIELD, buf, cap, nk_filter_default);
    return strcmp(before, buf) != 0;
}

bool form_engine(PgUi *ui, PgProject *p, bool wizard)
{
    const PgTheme *t = ui->theme;
    bool ch = false;
    PgEngine *e = &p->engine;

    if (!wizard) {
        ui_section(ui, "Project");
        ch |= edit_text(ui, "Title", "Printed on every drawing and used for "
                        "the export file names", p->title, sizeof p->title);
    }

    ui_section(ui, "Engine");
    ch |= edit_text(ui, "Name", "The engine, as it should appear on drawings",
                    e->name, sizeof e->name);
    ch |= ui_prop(ui, "Bore", "bore", "Cylinder bore", &e->bore_mm, 10, 300, 0.5, "mm");
    ch |= ui_prop(ui, "Stroke", "stroke", "Crank stroke", &e->stroke_mm, 10, 300, 0.5, "mm");
    ch |= ui_prop_int(ui, "Cylinders", "cyl", "One chamber per cylinder; "
                      "multiplies every part quantity", &e->cylinders, 1, 8, "");
    ch |= ui_prop(ui, "Exhaust duration", "exh",
                  "Crank degrees the exhaust port is open. The tuned length is "
                  "directly proportional to it — measure it",
                  &e->exh_duration_deg, 60, 260, 0.5, "\xc2\xb0");
    ch |= ui_prop(ui, "Outlet bore", "outlet",
                  "Internal diameter where the chamber starts. Every diameter "
                  "in the chamber is a multiple of it",
                  &e->outlet_dia_mm, 8, 200, 0.5, "mm");
    ch |= ui_prop(ui, "Duct in casting", "duct",
                  "Length of the exhaust passage from the port face to the "
                  "flange. It counts towards the header but is not made",
                  &e->duct_len_mm, 0, 500, 1, "mm");
    ch |= ui_prop(ui, "Port angled down", "portdown",
                  "How far below horizontal the exhaust leaves the cylinder",
                  &e->port_down_deg, -60, 60, 1, "\xc2\xb0");

    char buf[96];
    snprintf(buf, sizeof buf, "%.1f cc per cylinder", pg_displacement_cc(e));
    ui_info_row(ui, "Swept volume", buf);

    ui_label_wrap(ui, "Measure the exhaust duration with a degree wheel, and "
                  "the outlet bore and duct on the engine itself. The JLO L372 "
                  "preset's 155\xc2\xb0 is an owners' figure, and its 38 mm "
                  "outlet and 40 mm duct are placeholders.", t->warn);
    return ch;
}

bool form_tuning(PgUi *ui, PgProject *p)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    bool ch = false;
    PgTuning *tu = &p->tuning;

    ui_section(ui, "Objective");
    for (int o = 0; o < PG_OBJ_COUNT; o++) {
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        ui_tip(ui, pg_objective_blurb((PgObjective)o));
        if (nk_option_label(c, pg_objective_name((PgObjective)o),
                            tu->objective == (PgObjective)o) &&
            tu->objective != (PgObjective)o) {
            tu->objective = (PgObjective)o;
            pg_geometry_preset(tu->objective, &p->geom);
            ch = true;
        }
    }
    ui_label_wrap(ui, pg_objective_blurb(tu->objective), t->text_dim);
    ui_label_wrap(ui, "Choosing an objective resets the chamber proportions to "
                  "its preset.", t->text_faint);

    ui_section(ui, "Speed");
    if (tu->objective == PG_OBJ_POWER_BAND) {
        ch |= ui_prop(ui, "Band from", "rpmlo", "Bottom of the speed range",
                      &tu->rpm_lo, 300, 30000, 50, "rpm");
        ch |= ui_prop(ui, "Band to", "rpmhi", "Top of the speed range",
                      &tu->rpm_hi, 300, 30000, 50, "rpm");
        char buf[64];
        snprintf(buf, sizeof buf, "%.0f rpm", pg_design_rpm(tu));
        ui_info_row(ui, "Timed for", buf);
    } else {
        ch |= ui_prop(ui, "Design speed", "rpm",
                      "The speed the chamber is tuned for. A 50 Hz two-pole "
                      "generator runs a little under 3000 rpm under load",
                      &tu->rpm, 300, 30000, 25, "rpm");
    }
    ch |= ui_prop(ui, "Mean gas temp.", "gas",
                  "Average exhaust gas temperature in the chamber. Every length "
                  "scales with its square root; 420 C matches Jennings' rules",
                  &tu->gas_temp_c, 50, 1100, 5, "\xc2\xb0""C");
    return ch;
}

bool form_chamber(PgUi *ui, PgProject *p)
{
    struct nk_context *c = ui->ctx;
    PgGeometry *g = &p->geom;
    bool ch = false;

    ui_section(ui, "Lengths, as fractions of the tuned length");
    ch |= ui_prop(ui, "Header", "fh", "Port face to the start of the diffuser",
                  &g->f_header, 0.01, 0.9, 0.005, "");
    ch |= ui_prop(ui, "Diffuser", "fd", "The widening cone or cones",
                  &g->f_diffuser, 0.05, 0.9, 0.005, "");
    ch |= ui_prop(ui, "Belly", "fb", "The parallel middle section",
                  &g->f_belly, 0.0, 0.9, 0.005, "");
    ch |= ui_prop(ui, "Baffle", "fbf", "The narrowing cone. Its middle is at "
                  "the tuned length", &g->f_baffle, 0.05, 1.5, 0.005, "");
    ui_label_wrap(ui, "Normalised so header + diffuser + belly + half the baffle "
                  "is always the tuned length.", ui->theme->text_faint);

    ui_section(ui, "Diameters, as multiples of the outlet bore");
    ch |= ui_prop(ui, "Header exit", "taper", "Header taper: exit over entry",
                  &g->header_taper, 1.0, 2.0, 0.01, "\xc3\x97");
    ch |= ui_prop(ui, "Belly", "belly", "Belly diameter over outlet bore. "
                  "Usually 2.0-3.5", &g->belly_ratio, 1.2, 6.0, 0.02, "\xc3\x97");
    ch |= ui_prop(ui, "Stinger", "sting", "Stinger bore over outlet bore. "
                  "Usually 0.45-0.70; smaller runs hotter",
                  &g->stinger_ratio, 0.2, 1.2, 0.01, "\xc3\x97");

    ui_section(ui, "Shape");
    ch |= ui_prop_int(ui, "Diffuser stages", "stages",
                      "Cones of increasing angle; more stages spread the "
                      "response", &g->diffuser_stages, 1, 4, "");
    ch |= ui_prop(ui, "Stinger length", "stlen", "In stinger diameters",
                  &g->stinger_len_dia, 1, 40, 0.5, "\xc3\x97\xc3\x98");
    ch |= ui_prop(ui, "Plugging lead", "lead",
                  "Time the plugging pulse this many degrees before the port "
                  "closes at the design speed", &g->plug_lead_deg, -30, 30, 0.5,
                  "\xc2\xb0");

    ui_gap(ui, 4);
    nk_layout_row_dynamic(c, S(ui, 30), 1);
    char lab[96];
    snprintf(lab, sizeof lab, "Reset to the %s preset",
             pg_objective_name(p->tuning.objective));
    ui_tip(ui, "Put every proportion back to the objective's preset");
    if (nk_button_label(c, lab)) {
        pg_geometry_preset(p->tuning.objective, g);
        ch = true;
    }
    return ch;
}

bool form_build(PgUi *ui, PgProject *p)
{
    struct nk_context *c = ui->ctx;
    PgBuild *b = &p->build;
    bool ch = false;

    ui_section(ui, "Method");
    for (int m = 0; m < PG_MFG_COUNT; m++) {
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        ui_tip(ui, m == PG_MFG_ROLLED
               ? "Each piece rolled from a flat pattern and seam welded"
               : "Two flat halves welded round the edge and inflated");
        if (nk_option_label(c, pg_method_name((PgMethod)m), b->method == (PgMethod)m) &&
            b->method != (PgMethod)m) {
            b->method = (PgMethod)m;
            ch = true;
        }
    }

    ui_section(ui, "Material");
    for (int m = 0; m < PG_MAT_COUNT; m++) {
        nk_layout_row_dynamic(c, S(ui, 24), 1);
        if (nk_option_label(c, pg_material_name((PgMaterial)m),
                            b->material == (PgMaterial)m) &&
            b->material != (PgMaterial)m) {
            b->material = (PgMaterial)m;
            ch = true;
        }
    }
    ch |= ui_prop(ui, "Thickness", "thick", "Sheet thickness. Patterns are sized "
                  "on the mean diameter", &b->thickness_mm, 0.3, 6.0, 0.1, "mm");

    ui_section(ui, "Pieces");
    ch |= ui_prop_int(ui, "Segments", "segs",
                      "Pieces each section is cut into. The joints between them "
                      "are where the chamber can bend", &b->segments, 1, 8, "");
    ch |= ui_check(ui, "Header from tube", "Cut the header from tube instead of "
                   "rolling it", &b->header_tube);
    ch |= ui_check(ui, "Stinger from tube", "Cut the stinger from tube instead "
                   "of rolling it", &b->stinger_tube);
    if (b->method == PG_MFG_ROLLED) {
        ch |= ui_prop(ui, "Seam allowance", "allow", "Extra on one straight edge "
                      "for a lap seam; zero for a butt weld",
                      &b->seam_allow_mm, 0, 30, 0.5, "mm");
        ch |= ui_prop(ui, "Seam position", "seam", "Where round the pipe the seam "
                      "welds sit; 0 is underneath at the port",
                      &p->route.seam_deg, 0, 359, 5, "\xc2\xb0");
    } else {
        ch |= ui_prop(ui, "Weld margin", "margin", "Flat added each side, "
                      "consumed by the edge weld", &b->hydro_margin_mm, 0, 30, 0.5, "mm");
        ch |= ui_prop(ui, "Seam plane", "seamh", "Rotates the plane of the edge "
                      "welds. Bends can only be made in this plane",
                      &p->route.seam_deg, 0, 359, 5, "\xc2\xb0");
        ch |= ui_check(ui, "Seam-length compensation", "Keep each flat edge as "
                       "long as the inflated seam it becomes", &b->hydro_seam_comp);
    }
    ch |= ui_check(ui, "Alignment marks", "Etch-layer ticks at quarter "
                   "circumferences and the inside of every bend", &b->etch_marks);

    ui_section(ui, "Sheet");
    ch |= ui_prop(ui, "Sheet width", "sw", "Stock sheet size", &b->sheet_w_mm,
                  100, 10000, 10, "mm");
    ch |= ui_prop(ui, "Sheet height", "sh", "Stock sheet size", &b->sheet_h_mm,
                  100, 10000, 10, "mm");
    ch |= ui_prop(ui, "Gap", "gap", "Space between parts and round the edge",
                  &b->part_gap_mm, 0, 100, 1, "mm");
    return ch;
}

/* ------------------------------------------------------------------ */
/* Inspector tabs                                                      */
/* ------------------------------------------------------------------ */

static void panel_overview(PgUi *ui)
{
    const PgTheme *t = ui->theme;
    const PgModel *m = ui->model;
    const PgDesign *d = &m->design;

    ui_label_wrap(ui, "Click a piece or a joint ring in the view to select it. "
                  "Select a joint, then Ctrl+drag to bend it: up and down for "
                  "the angle, sideways for the direction.", t->text_faint);

    ui_section(ui, "Key figures");
    ui_info_rowf(ui, "Tuned length", "%.0f mm", d->tuned_len);
    ui_info_rowf(ui, "Pipe tuned for", "%.0f rpm", d->tuned_rpm);
    ui_info_rowf(ui, "On time", "%.0f \xe2\x80\x93 %.0f rpm", d->band_lo, d->band_hi);
    ui_info_rowf(ui, "Wave speed", "%.0f m/s", d->wave_speed);
    ui_info_rowf(ui, "Exhaust", "opens %.0f\xc2\xb0, closes %.0f\xc2\xb0",
                 d->epo_deg, d->epc_deg);
    ui_info_rowf(ui, "Outlet / belly", "\xc3\x98%.1f / \xc3\x98%.1f mm",
                 d->d_outlet, d->d_belly);
    ui_info_rowf(ui, "Stinger", "\xc3\x98%.1f mm", d->d_stinger);
    ui_info_rowf(ui, "Centreline", "%.0f mm", d->total_len);
    ui_info_rowf(ui, "Volume", "%.2f L", d->volume_litres);
    if (d->silencer_litres > 0.0)
        ui_info_rowf(ui, "Silencer", "at least %.1f L", d->silencer_litres);
    ui_info_rowf(ui, "Sheet parts", "%.2f m\xc2\xb2, %.1f kg",
                 m->parts->sheet_area_mm2 / 1e6, m->parts->mass_kg);
    ui_info_rowf(ui, "Sheets", "%d \xc3\x97 %.0f \xc3\x97 %.0f mm",
                 m->layout->n_sheets, m->layout->sheet_w, m->layout->sheet_h);

    int n = d->n_warn + m->chain->n_warn + m->parts->n_warn;
    if (n || m->layout->n_oversize) {
        ui_section(ui, "Check");
        for (int i = 0; i < m->chain->n_warn; i++)
            ui_label_wrap(ui, m->chain->warn[i], t->alarm);
        for (int i = 0; i < d->n_warn; i++)
            ui_label_wrap(ui, d->warn[i], t->warn);
        for (int i = 0; i < m->parts->n_warn; i++)
            ui_label_wrap(ui, m->parts->warn[i], t->warn);
        if (m->layout->n_oversize)
            ui_label_wrap(ui, "A part is larger than the sheet in every "
                          "orientation. Use more segments or a larger sheet.",
                          t->warn);
    }
}

static void section_params(PgUi *ui, PgSectionKind k)
{
    PgProject *p = &ui->model->project;
    PgGeometry *g = &p->geom;
    switch (k) {
    case PG_SEC_HEADER:
        ui_prop(ui, "Header fraction", "s_fh", "Header length over tuned length",
                &g->f_header, 0.01, 0.9, 0.005, "");
        ui_prop(ui, "Exit / outlet", "s_taper", "Header taper", &g->header_taper,
                1.0, 2.0, 0.01, "\xc3\x97");
        ui_prop(ui, "Duct in casting", "s_duct", "Not fabricated",
                &p->engine.duct_len_mm, 0, 500, 1, "mm");
        break;
    case PG_SEC_DIFFUSER:
        ui_prop(ui, "Diffuser fraction", "s_fd", "Diffuser length over tuned length",
                &g->f_diffuser, 0.05, 0.9, 0.005, "");
        ui_prop_int(ui, "Stages", "s_stages", "Cones of increasing angle",
                    &g->diffuser_stages, 1, 4, "");
        break;
    case PG_SEC_BELLY:
        ui_prop(ui, "Belly fraction", "s_fb", "Belly length over tuned length",
                &g->f_belly, 0.0, 0.9, 0.005, "");
        ui_prop(ui, "Belly / outlet", "s_belly", "Belly diameter", &g->belly_ratio,
                1.2, 6.0, 0.02, "\xc3\x97");
        break;
    case PG_SEC_BAFFLE:
        ui_prop(ui, "Baffle fraction", "s_fbf", "Baffle length over tuned length",
                &g->f_baffle, 0.05, 1.5, 0.005, "");
        break;
    case PG_SEC_STINGER:
        ui_prop(ui, "Stinger / outlet", "s_st", "Stinger bore", &g->stinger_ratio,
                0.2, 1.2, 0.01, "\xc3\x97");
        ui_prop(ui, "Length", "s_stl", "In stinger diameters", &g->stinger_len_dia,
                1, 40, 0.5, "\xc3\x97\xc3\x98");
        ui_check(ui, "From tube", "Cut the stinger from tube", &p->build.stinger_tube);
        break;
    }
}

static void panel_piece(PgUi *ui, int i)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgModel *m = ui->model;
    const PgPiece *pc = &m->chain->piece[i];
    const PgSection *s = &m->design.sec[pc->section];

    nk_layout_row_dynamic(c, S(ui, 26), 1);
    char head[128];
    snprintf(head, sizeof head, "%s%s%s", pc->part >= 0 ? m->parts->part[pc->part].id : "",
             pc->part >= 0 ? "  " : "", pc->name);
    nk_label_colored(c, head, NK_TEXT_LEFT, t->accent);

    const char *kind = pc->kind == PG_PIECE_DUCT ? "exhaust duct, not made"
                     : pc->kind == PG_PIECE_TUBE ? "cut from tube"
                     : m->project.build.method == PG_MFG_HYDRO ? "hydroformed"
                     : fabs(pc->d1 - pc->d0) < 0.05 ? "rolled cylinder" : "rolled cone";
    ui_info_row(ui, "Made as", kind);
    ui_info_rowf(ui, "From the port", "%.0f to %.0f mm", pc->x0, pc->x0 + pc->len);
    ui_info_rowf(ui, "Length", "%.1f mm", pc->len);
    ui_info_rowf(ui, "Bore", "\xc3\x98%.1f to \xc3\x98%.1f mm", pc->d0, pc->d1);
    ui_info_rowf(ui, "Wall angle", "%.2f\xc2\xb0", pg_section_half_angle(
        &(PgSection){ .len = pc->len, .d0 = pc->d0, .d1 = pc->d1 }));
    if (pc->cut0_deg > 1e-6 || pc->cut1_deg > 1e-6)
        ui_info_rowf(ui, "Mitre cuts", "%.1f\xc2\xb0 / %.1f\xc2\xb0",
                     pc->cut0_deg, pc->cut1_deg);
    if (pc->bad_mitre)
        ui_label_wrap(ui, "Too short for the bends at its ends: the mitre cuts "
                      "cross.", t->alarm);
    if (pc->clash)
        ui_label_wrap(ui, "Leaves the clearance box, or runs into the engine or "
                      "the chamber.", t->alarm);

    ui_gap(ui, 4);
    nk_layout_row_dynamic(c, S(ui, 30), 2);
    ui_tip(ui, "Select the joint at the start of this piece");
    if (i > 0) {
        if (nk_button_label(c, "\xe2\x86\x90 Joint"))
            select_id(ui, PG_PICK_JOINT + i - 1);
    } else {
        nk_skip(c);
    }
    ui_tip(ui, "Select the joint at the end of this piece");
    if (i < m->chain->n_joints) {
        if (nk_button_label(c, "Joint \xe2\x86\x92"))
            select_id(ui, PG_PICK_JOINT + i);
    } else {
        nk_skip(c);
    }
    if (pc->part >= 0) {
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Open this part's flat pattern on the Patterns page");
        if (nk_button_label(c, "Show flat pattern")) {
            ui->part_sel = pc->part;
            ui->pattern_tab = 0;
            ui->page = PAGE_PATTERNS;
        }
    }

    char sec[96];
    snprintf(sec, sizeof sec, "Section: %s", s->name);
    ui_section(ui, sec);
    ui_info_rowf(ui, "Section length", "%.0f mm", s->len);
    section_params(ui, s->kind);
}

static void panel_joint(PgUi *ui, int j)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgModel *m = ui->model;
    const PgChain *ch = m->chain;
    const PgJoint *jt = &ch->joint[j];
    bool hydro = m->project.build.method == PG_MFG_HYDRO;

    nk_layout_row_dynamic(c, S(ui, 26), 1);
    char head[128];
    snprintf(head, sizeof head, "Joint %d  \xc2\xb7  %.0f mm from the port", j + 1, jt->x);
    nk_label_colored(c, head, NK_TEXT_LEFT, t->accent);
    ui_info_rowf(ui, "Between", "%s", ch->piece[jt->before].name);
    ui_info_rowf(ui, "and", "%s", ch->piece[jt->after].name);

    ui_section(ui, "Bend");
    double bend = jt->bend_deg, roll = jt->roll_deg;
    bool changed = false;
    changed |= ui_prop(ui, "Angle", "jbend", "How far the pipe turns at this "
                       "joint. Each side is mitred at half of it",
                       &bend, 0, 90, 1, "\xc2\xb0");
    changed |= ui_prop(ui, "Direction", "jroll", hydro
                       ? "Hydroformed: turned onto the seam plane"
                       : "Which way it turns, round the pipe: 0 down, 180 up, 90 "
                         "and 270 to the sides (at the port)",
                       &roll, 0, 359, 5, "\xc2\xb0");
    ui_info_row(ui, "Turns", bend > 1e-6 ? pg_roll_name(jt->roll_deg) : "straight");
    ui_info_rowf(ui, "Limit here", "%.0f\xc2\xb0 a joint", jt->limit_deg);
    if (jt->sharp)
        ui_label_wrap(ui, jt->bend_deg > jt->limit_deg
                      ? "Sharper than this section should turn at one joint. "
                        "Spread the turn over more joints, or use Route > "
                        "Auto-fold."
                      : "Part of a run of bends tighter than twice the pipe's "
                        "diameter. Spread it over longer pieces or fewer "
                        "segments.", t->warn);
    if (changed)
        set_joint(ui, j, bend, roll);

    nk_layout_row_dynamic(c, S(ui, 28), 4);
    static const struct { const char *label; double roll; const char *tip; } dirs[] = {
        { "Down", 0,   "Turn towards the reference direction (down at the port)" },
        { "-Z",   90,  "Turn a quarter round from down" },
        { "Up",   180, "Turn away from the reference direction (up at the port)" },
        { "+Z",   270, "Turn three quarters round from down" },
    };
    for (int i = 0; i < 4; i++) {
        ui_tip(ui, dirs[i].tip);
        if (nk_button_label(c, dirs[i].label))
            set_joint(ui, j, bend > 1e-6 ? bend : 30.0, dirs[i].roll);
    }
    nk_layout_row_dynamic(c, S(ui, 28), 4);
    static const double quick[4] = { 15, 30, 45, 90 };
    for (int i = 0; i < 4; i++) {
        char lab[16];
        snprintf(lab, sizeof lab, "%.0f\xc2\xb0", quick[i]);
        ui_tip(ui, "Set the bend angle");
        if (nk_button_label(c, lab))
            set_joint(ui, j, quick[i], roll);
    }

    nk_layout_row_dynamic(c, S(ui, 30), 3);
    ui_tip(ui, "Previous joint (Page Up)");
    if (nk_button_label(c, "\xe2\x86\x90 Prev"))
        select_joint_step(ui, -1);
    ui_tip(ui, "Remove the bend (Delete)");
    if (nk_button_label(c, "Straighten"))
        set_joint(ui, j, 0.0, 0.0);
    ui_tip(ui, "Next joint (Page Down)");
    if (nk_button_label(c, "Next \xe2\x86\x92"))
        select_joint_step(ui, +1);

    const PgPiece *a = &ch->piece[jt->before], *b = &ch->piece[jt->after];
    if (jt->bend_deg > 1e-6)
        ui_info_rowf(ui, "Mitres", "%.1f\xc2\xb0 each side", a->cut1_deg);
    if (a->bad_mitre || b->bad_mitre)
        ui_label_wrap(ui, "A piece beside this joint is too short for its "
                      "mitres. Bend less or use fewer segments.", t->alarm);

    ui_gap(ui, 2);
    ui_label_wrap(ui, "In the view: Ctrl+drag up and down to change the angle, "
                  "sideways to turn the direction; add Shift to step. Keys: "
                  "[ and ] angle, comma and full stop direction.", t->text_faint);
    if (hydro)
        ui_label_wrap(ui, "Hydroformed chambers bend only in the plane of their "
                      "seams; the direction is turned onto it.", t->warn);
}

/* ------------------------------------------------------------------ */
/* Auto-fold                                                           */
/* ------------------------------------------------------------------ */

void design_tick(PgUi *ui)
{
    if (!ui->folder)
        return;

    /* the search started from the project as it was; if that has changed,
     * its answer is for a different chamber */
    if (strcmp(ui->cur_text, ui->fold_text) != 0) {
        pg_fold_end(ui->folder);
        ui->folder = NULL;
        ui_message(ui, false, "Auto-fold stopped: the project changed while it "
                   "was searching.");
        return;
    }

    /* a slice that keeps the interface at a usable frame rate */
    uint64_t t0 = plat_now_ms();
    bool done;
    do {
        done = pg_fold_step(ui->folder, 25);
    } while (!done && plat_now_ms() - t0 < 30);
    if (!done)
        return;

    const PgFoldResult *r = pg_fold_result(ui->folder);
    snprintf(ui->fold_msg, sizeof ui->fold_msg, "%s", r->summary);
    ui->fold_ok = r->ok;
    if (r->ok) {
        PgProject *p = &ui->model->project;
        ui->fold_prev_route = p->route;
        ui->fold_prev_segments = p->build.segments;
        pg_fold_apply(p, r);
        ui->fold_can_revert = true;
        ui->fit_pending = true;
        ui->vopt.select_id = -1;
        ui_message(ui, false, "Folded. Ctrl+Z, or Revert on the Route tab, puts "
                   "it back.");
    } else {
        ui_message(ui, true, "Auto-fold found no safe layout; the chamber was "
                   "left as it was.");
    }
    pg_fold_end(ui->folder);
    ui->folder = NULL;
}

static void panel_autofold(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgProject *p = &ui->model->project;
    PgFoldOpts *o = &ui->fold;

    ui_section(ui, "Auto-fold");

    if (ui->folder) {
        double prog = pg_fold_progress(ui->folder);
        nk_layout_row_dynamic(c, S(ui, 20), 1);
        nk_size v = (nk_size)(prog * 1000.0);
        nk_progress(c, &v, 1000, nk_false);
        char buf[96];
        snprintf(buf, sizeof buf, "Searching for the best safe layout \xe2\x80\x94 "
                 "%.0f%%", prog * 100.0);
        nk_layout_row_dynamic(c, S(ui, 22), 1);
        nk_label_colored(c, buf, NK_TEXT_LEFT, t->text_dim);
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Stop searching; the chamber is left as it is");
        if (nk_button_label(c, "Stop")) {
            pg_fold_end(ui->folder);
            ui->folder = NULL;
            ui_message(ui, false, "Auto-fold stopped.");
        }
        return;
    }

    ui_label_wrap(ui, "Folds the chamber into the best layout that keeps every "
                  "rule: no joint sharper than its section allows, runs of bends "
                  "no tighter than twice the diameter, and clear of the engine, "
                  "the box and itself by the gaps below. It replaces the bends "
                  "there are now.", t->text_faint);

    bool box = p->clear.enabled;
    if (!box && o->goal == PG_FOLD_FIT_BOX)
        o->goal = PG_FOLD_COMPACT;
    nk_layout_row_dynamic(c, S(ui, 24), 2);
    ui_tip(ui, "The smallest package, engine included");
    if (nk_option_label(c, "Most compact", o->goal == PG_FOLD_COMPACT))
        o->goal = PG_FOLD_COMPACT;
    if (!box) nk_widget_disable_begin(c);
    ui_tip(ui, box ? "Inside (or out of) the clearance box, with the most room to spare"
                   : "Set a clearance box first, on the Clearance tab");
    if (nk_option_label(c, "Fit the box", o->goal == PG_FOLD_FIT_BOX) && box)
        o->goal = PG_FOLD_FIT_BOX;
    if (!box) nk_widget_disable_end(c);

    bool hydro = p->build.method == PG_MFG_HYDRO;
    static const struct { const char *l; PgFoldPlane v; const char *tip; } planes[] = {
        { "Any way", PG_FOLD_ANY,     "Turns in any direction" },
        { "Flat",    PG_FOLD_FLAT,    "Sideways only: the chamber stays at port height" },
        { "Upright", PG_FOLD_UPRIGHT, "Up and down only: the chamber stays in one "
                                      "vertical plane" },
    };
    nk_layout_row_dynamic(c, S(ui, 24), 3);
    if (hydro) nk_widget_disable_begin(c);
    for (int i = 0; i < 3; i++) {
        ui_tip(ui, planes[i].tip);
        if (nk_option_label(c, planes[i].l, o->plane == planes[i].v) && !hydro)
            o->plane = planes[i].v;
    }
    if (hydro) nk_widget_disable_end(c);
    if (hydro)
        ui_label_wrap(ui, "Hydroformed: it folds only in the plane of the seams.",
                      t->text_faint);

    ui_prop_int(ui, "Most turns", "f_turns", "The most separate turns the fold may "
                "use; each is spread over several joints", &o->max_turns, 1, 4, "");
    if (o->max_segments < p->build.segments)
        o->max_segments = p->build.segments;
    ui_prop_int(ui, "Up to segments", "f_segs", "It may cut sections into more "
                "pieces, up to this many, when a gentler turn needs more joints",
                &o->max_segments, p->build.segments, 8, "");
    ui_prop(ui, "Heat gap", "f_heat", "Keep at least this far from the engine and "
            "from other parts of the chamber: both change the gas temperature, "
            "and so the tuning", &o->heat_gap_mm, 0, 300, 5, "mm");
    if (box)
        ui_prop(ui, "Box wall gap", "f_wall", "Keep at least this far from the "
                "clearance box's walls", &o->wall_gap_mm, 0, 200, 5, "mm");

    static const char *effort[3] = { "Quick", "Normal", "Thorough" };
    static const char *effort_tip[3] = {
        "A short search: a good layout in a second or two",
        "A longer search, usually a better layout",
        "The longest search, for a tight box",
    };
    nk_layout_row_dynamic(c, S(ui, 26), 3);
    for (int i = 0; i < 3; i++) {
        nk_bool on = o->effort == i + 1;
        ui_tip(ui, effort_tip[i]);
        if (nk_selectable_label(c, effort[i], NK_TEXT_CENTERED, &on) && on)
            o->effort = i + 1;
    }

    ui_gap(ui, 2);
    nk_layout_row_dynamic(c, S(ui, 32), 1);
    ui_tip(ui, ui->fold_msg[0] ? "Search again from a different start; another "
                                 "safe layout may score better"
                               : "Search for the best safe fold and apply it. "
                                 "Ctrl+Z undoes it");
    if (ui_primary_button(ui, ui->fold_msg[0] ? "Fold again" : "Fold it")) {
        o->seed++;                     /* each run starts somewhere new */
        memcpy(ui->fold_text, ui->cur_text, TEXT_MAX);
        ui->folder = pg_fold_begin(p, o);
        ui->fold_msg[0] = '\0';
        if (!ui->folder)
            ui_message(ui, true, "Out of memory.");
    }

    if (ui->fold_msg[0])
        ui_label_wrap(ui, ui->fold_msg, ui->fold_ok ? t->ok : t->warn);
    if (ui->fold_can_revert) {
        nk_layout_row_dynamic(c, S(ui, 30), 1);
        ui_tip(ui, "Put the bends and segments back as they were before the fold");
        if (nk_button_label(c, "Revert to before the fold")) {
            p->route = ui->fold_prev_route;
            p->build.segments = ui->fold_prev_segments;
            ui->fold_can_revert = false;
            ui->fold_msg[0] = '\0';
            ui->fit_pending = true;
        }
    }
}

static void panel_route(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgModel *m = ui->model;
    const PgChain *ch = m->chain;

    panel_autofold(ui);

    ui_section(ui, "Bends");
    ui_label_wrap(ui, "Bends are made at the joints between pieces. Select a "
                  "joint ring in the view, or one below.", t->text_faint);

    int shown = 0;
    for (int j = 0; j < ch->n_joints; j++) {
        const PgJoint *jt = &ch->joint[j];
        if (jt->bend_deg < 1e-6)
            continue;
        shown++;
        nk_layout_row_template_begin(c, S(ui, 26));
        nk_layout_row_template_push_dynamic(c);
        nk_layout_row_template_push_static(c, S(ui, 62));
        nk_layout_row_template_push_static(c, S(ui, 30));
        nk_layout_row_template_end(c);
        char lab[128];
        snprintf(lab, sizeof lab, "J%d  %.0f mm   %.0f\xc2\xb0 at %.0f\xc2\xb0%s",
                 j + 1, jt->x, jt->bend_deg, jt->roll_deg, jt->sharp ? "  sharp" : "");
        nk_bool on = ui->vopt.select_id == PG_PICK_JOINT + j;
        ui_tip(ui, "Select this joint");
        if (nk_selectable_label(c, lab, NK_TEXT_LEFT, &on) && on)
            select_id(ui, PG_PICK_JOINT + j), ui->insp = INSP_ROUTE;
        ui_tip(ui, "Select it and show its bend");
        if (nk_button_label(c, "Edit"))
            select_id(ui, PG_PICK_JOINT + j);
        ui_tip(ui, "Straighten this joint");
        if (nk_button_label(c, "\xc3\x97"))
            set_joint(ui, j, 0.0, 0.0);
    }
    if (!shown) {
        nk_layout_row_dynamic(c, S(ui, 22), 1);
        nk_label_colored(c, "The chamber is straight.", NK_TEXT_LEFT, t->text_faint);
    }

    ui_gap(ui, 4);
    nk_layout_row_dynamic(c, S(ui, 30), 1);
    if (shown) {
        ui_tip(ui, "Remove every bend");
        if (nk_button_label(c, "Straighten all"))
            m->project.route.n_bends = 0;
    }

    ui_section(ui, "Seams");
    ui_prop(ui, m->project.build.method == PG_MFG_HYDRO ? "Seam plane" : "Seam position",
            "r_seam", "Where round the pipe the seam welds sit; 0 is underneath "
            "at the port", &m->project.route.seam_deg, 0, 359, 5, "\xc2\xb0");

    for (int i = 0; i < ch->n_warn; i++)
        ui_label_wrap(ui, ch->warn[i], t->alarm);
}

static void panel_clearance(PgUi *ui)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;
    PgModel *m = ui->model;
    PgClearance *cl = &m->project.clear;
    const PgChain *ch = m->chain;

    ui_label_wrap(ui, "A box in engine coordinates: +X out of the exhaust port, "
                  "+Y up the cylinder, Z along the crankshaft, origin on the "
                  "cylinder axis at port height.", t->text_faint);
    ui_check(ui, "Use a clearance box", "Check every piece against the box",
             &cl->enabled);
    if (!cl->enabled)
        return;

    nk_layout_row_dynamic(c, S(ui, 24), 2);
    ui_tip(ui, "The chamber must stay inside the box: a frame or enclosure");
    if (nk_option_label(c, "Keep inside", cl->keep_inside))
        cl->keep_inside = true;
    ui_tip(ui, "The chamber must stay out of the box: a tank, a wheel, a battery");
    if (nk_option_label(c, "Keep out", !cl->keep_inside))
        cl->keep_inside = false;

    static const char *axis[3] = { "X", "Y", "Z" };
    for (int i = 0; i < 3; i++) {
        char lab[32], id[16];
        snprintf(lab, sizeof lab, "%s from", axis[i]);
        snprintf(id, sizeof id, "cmin%d", i);
        ui_prop(ui, lab, id, "Box minimum", &cl->min[i], -20000, 20000, 10, "mm");
        snprintf(lab, sizeof lab, "%s to", axis[i]);
        snprintf(id, sizeof id, "cmax%d", i);
        ui_prop(ui, lab, id, "Box maximum", &cl->max[i], -20000, 20000, 10, "mm");
    }

    nk_layout_row_dynamic(c, S(ui, 30), 1);
    ui_tip(ui, "Set the box to the chamber's present extent plus 50 mm");
    if (nk_button_label(c, "Fit the box round the chamber")) {
        for (int i = 0; i < 3; i++) {
            cl->min[i] = floor((ch->bmin[i] - 50.0) / 10.0) * 10.0;
            cl->max[i] = ceil((ch->bmax[i] + 50.0) / 10.0) * 10.0;
        }
    }

    ui_gap(ui, 4);
    if (ch->n_clash_box)
        ui_label_wrap(ui, cl->keep_inside
                      ? "Pieces leave the box; they are shown in red."
                      : "Pieces enter the box; they are shown in red.", t->alarm);
    else
        ui_label_wrap(ui, "Every piece is clear.", t->ok);
}

static void inspector(PgUi *ui, float h)
{
    struct nk_context *c = ui->ctx;
    static const char *TAB[INSP_COUNT] = {
        "Selection", "Engine", "Tuning", "Chamber", "Build", "Route", "Clearance"
    };
    static const char *TAB_HINT[INSP_COUNT] = {
        "What is selected in the view, or the key figures",
        "Bore, stroke, port timing and outlet",
        "Objective, design speed, gas temperature",
        "Section proportions",
        "Method, material, segments, sheet",
        "Bends and seams",
        "The optional clearance box",
    };

    if (nk_group_begin(c, "inspector", NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        for (int row = 0; row < 2; row++) {
            nk_layout_row_dynamic(c, S(ui, 26), row == 0 ? 4 : 3);
            for (int i = row == 0 ? 0 : 4; i < (row == 0 ? 4 : INSP_COUNT); i++) {
                nk_bool on = ui->insp == (Inspector)i;
                ui_tip(ui, TAB_HINT[i]);
                if (nk_selectable_label(c, TAB[i], NK_TEXT_CENTERED, &on) && on)
                    ui->insp = (Inspector)i;
            }
        }

        /* The tab row is full, so this peeks at where the next row starts.
         * A zero-height probe row would itself take a whole row's height. */
        float top = nk_widget_bounds(c).y;
        struct nk_rect gr = nk_window_get_content_region(c);
        float body = gr.y + gr.h - top - c->style.window.spacing.y;
        (void)h;
        if (body < S(ui, 80))
            body = S(ui, 80);

        nk_layout_row_dynamic(c, body, 1);
        if (nk_group_begin(c, "inspbody", 0)) {
            PgProject *p = &ui->model->project;
            int sel = ui->vopt.select_id;
            switch (ui->insp) {
            case INSP_SELECT:
                if (PG_PICK_IS_PIECE(sel))
                    panel_piece(ui, sel);
                else if (PG_PICK_IS_JOINT(sel))
                    panel_joint(ui, sel - PG_PICK_JOINT);
                else
                    panel_overview(ui);
                break;
            case INSP_ENGINE:  form_engine(ui, p, false); break;
            case INSP_TUNING:  form_tuning(ui, p);        break;
            case INSP_CHAMBER: form_chamber(ui, p);       break;
            case INSP_BUILD:   form_build(ui, p);         break;
            case INSP_ROUTE:   panel_route(ui);           break;
            case INSP_CLEAR:   panel_clearance(ui);       break;
            default: break;
            }
            ui_gap(ui, 8);
            nk_group_end(c);
        }
        nk_group_end(c);
    }
}

/* ------------------------------------------------------------------ */
/* Page                                                                */
/* ------------------------------------------------------------------ */

static bool tool_toggle(PgUi *ui, const char *label, const char *tip, bool *v)
{
    nk_bool b = *v;
    ui_tip(ui, tip);
    nk_checkbox_label(ui->ctx, label, &b);
    if ((b != 0) != *v) {
        *v = b != 0;
        ui->view_dirty = true;
        return true;
    }
    return false;
}

void page_design(PgUi *ui, struct nk_rect r)
{
    struct nk_context *c = ui->ctx;
    const PgTheme *t = ui->theme;

    validate_selection(ui);

    /* ---- toolbar ---- */
    nk_layout_row_template_begin(c, S(ui, ROW_H));
    nk_layout_row_template_push_static(c, S(ui, 46));   /* fit   */
    nk_layout_row_template_push_static(c, S(ui, 44));   /* 3/4   */
    nk_layout_row_template_push_static(c, S(ui, 48));   /* side  */
    nk_layout_row_template_push_static(c, S(ui, 44));   /* top   */
    nk_layout_row_template_push_static(c, S(ui, 44));   /* end   */
    nk_layout_row_template_push_static(c, S(ui, 14));
    nk_layout_row_template_push_static(c, S(ui, 70));   /* colour */
    nk_layout_row_template_push_static(c, S(ui, 50));
    nk_layout_row_template_push_static(c, S(ui, 60));
    nk_layout_row_template_push_static(c, S(ui, 14));
    nk_layout_row_template_push_static(c, S(ui, 80));   /* engine */
    nk_layout_row_template_push_static(c, S(ui, 66));   /* box    */
    nk_layout_row_template_push_static(c, S(ui, 72));   /* seams  */
    nk_layout_row_template_push_static(c, S(ui, 62));   /* grid   */
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_end(c);

    ui_tip(ui, "Frame the whole chamber (F, or double-click the view)");
    if (nk_button_label(c, "Fit"))
        ui->fit_pending = true;
    static const struct { const char *l; PgViewPreset v; const char *tip; } views[] = {
        { "3/4",  PG_VIEW_3Q,   "Three-quarter view (1)" },
        { "Side", PG_VIEW_SIDE, "From the side, looking along the crankshaft (2)" },
        { "Top",  PG_VIEW_TOP,  "From above (3)" },
        { "End",  PG_VIEW_END,  "Looking back at the engine along the port (4)" },
    };
    for (int i = 0; i < 4; i++) {
        ui_tip(ui, views[i].tip);
        if (nk_button_label(c, views[i].l)) {
            pg_camera_preset(&ui->cam, views[i].v);
            ui->fit_pending = true;
        }
    }
    nk_skip(c);
    static const struct { const char *l; PgColourMode m; const char *tip; } cols[] = {
        { "Sections", PG_COLOUR_SECTION, "Colour by section: header, diffuser, belly, baffle, stinger" },
        { "Parts",    PG_COLOUR_PART,    "Alternate shades per flat part, so every joint shows" },
        { "Metal",    PG_COLOUR_METAL,   "Plain metal" },
    };
    for (int i = 0; i < 3; i++) {
        nk_bool on = ui->vopt.colour == cols[i].m;
        ui_tip(ui, cols[i].tip);
        if (nk_selectable_label(c, cols[i].l, NK_TEXT_CENTERED, &on) && on &&
            ui->vopt.colour != cols[i].m) {
            ui->vopt.colour = cols[i].m;
            ui->view_dirty = true;
        }
    }
    nk_skip(c);
    tool_toggle(ui, "Engine", "Show the engine for scale and clearance",
                &ui->vopt.show_engine);
    tool_toggle(ui, "Box", "Show the clearance box, when one is set",
                &ui->vopt.show_box);
    tool_toggle(ui, "Seams", "Show seam welds, or hydroforming flanges",
                &ui->vopt.show_seams);
    tool_toggle(ui, "Grid", "Show the ground grid", &ui->vopt.show_grid);
    nk_label_colored(c, "drag: orbit  \xc2\xb7  right drag: pan  \xc2\xb7  "
                     "wheel: zoom", NK_TEXT_RIGHT, t->text_faint);

    /* ---- body: view and inspector ---- */
    float top = nk_widget_bounds(c).y;          /* the toolbar row is full */
    float body_h = r.y + r.h - top - c->style.window.spacing.y;
    if (body_h < S(ui, 200))
        body_h = S(ui, 200);

    nk_layout_row_template_begin(c, body_h);
    nk_layout_row_template_push_dynamic(c);
    nk_layout_row_template_push_static(c, S(ui, INSPECTOR_W));
    nk_layout_row_template_end(c);

    struct nk_rect vr = nk_widget_bounds(c);
    nk_skip(c);
    vr.x = floorf(vr.x);
    vr.y = floorf(vr.y);
    vr.w = floorf(vr.w);
    vr.h = floorf(vr.h);
    ui->view_rect = vr;

    view_input(ui, vr);
    render_view(ui, (int)vr.w, (int)vr.h);

    if (ui->tex) {
        struct nk_image img = nk_image_ptr(ui->tex);
        nk_draw_image(nk_window_get_canvas(c), vr, &img, nk_rgba(255, 255, 255, 255));
    }
    draw_overlays(ui, vr);

    inspector(ui, body_h);
}
