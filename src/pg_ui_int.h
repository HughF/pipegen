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
 * pg_ui_int.h — what the interface's own files share
 *
 * The interface is split by page: pg_ui.c is the shell (status strip, rail,
 * action bar, tooltips, file handling, undo), pg_ui_design.c the 3D design
 * page and its inspector, pg_ui_pages.c the other pages, pg_ui_dialogs.c the
 * wizard and the dialogs. Not for use outside those files.
 *
 * Same layout rule as svpview and magview: the shell computes an exact pixel
 * rect for every region, and each region is its own Nuklear window at it.
 */
#ifndef PG_UI_INT_H
#define PG_UI_INT_H

#include <SDL2/SDL.h>
#include <stdarg.h>

#include "nk.h"
#include "pg_ui.h"
#include "pg_theme.h"
#include "pg_model.h"
#include "pg_config.h"
#include "pg_view3d.h"
#include "pg_glview.h"
#include "pg_draw.h"
#include "pg_autofold.h"
#include "plat.h"

/* Unscaled metrics, multiplied by the HiDPI scale at use. */
#define STATUS_H     46.0f
#define ACTION_H     32.0f
#define RAIL_W      150.0f
#define ROW_H        26.0f
#define LABEL_W     150.0f
#define UNIT_W       46.0f
#define BTN_W        96.0f
#define BTN_H        32.0f
#define INSPECTOR_W 390.0f
#define DLG_BOTTOM_MARGIN 12.0f

typedef enum {
    PAGE_DESIGN = 0, PAGE_PATTERNS, PAGE_PROFILE, PAGE_EXPORT, PAGE_HELP,
    PAGE_COUNT
} PgPage;

typedef enum {
    DLG_NONE = 0, DLG_WIZARD, DLG_FILE, DLG_CONFIRM, DLG_ABOUT
} PgDialog;

typedef enum { DLG_R_NONE, DLG_R_CANCEL, DLG_R_OK, DLG_R_ALT } DlgResult;

typedef enum { FILE_OPEN, FILE_SAVE } FileMode;

/* What to do once an unsaved-changes question is answered. */
typedef enum {
    PENDING_NONE = 0, PENDING_NEW, PENDING_OPEN, PENDING_OPEN_PATH, PENDING_QUIT
} Pending;

typedef enum {
    INSP_SELECT = 0, INSP_ENGINE, INSP_TUNING, INSP_CHAMBER, INSP_BUILD,
    INSP_ROUTE, INSP_CLEAR, INSP_COUNT
} Inspector;

typedef enum { DRAG_NONE, DRAG_PENDING, DRAG_ORBIT, DRAG_PAN, DRAG_BEND } DragMode;

#define UNDO_MAX     64
#define TEXT_MAX     16384
#define FILE_ENTRIES 512

struct PgUi {
    struct nk_context *ctx;
    SDL_Window        *win;
    SDL_Renderer      *ren;              /* NULL when drawing with OpenGL */
    char               renderer[200];

    PgModel    *model;
    PgSettings  settings;

    const PgTheme *theme;
    bool     dark;
    bool     theme_toggle;
    float    scale;
    PgPage   page, last_page;
    bool     quit;

    /* the project on disk */
    char     path[PLAT_PATH_MAX];
    char    *saved_text;             /* what the file holds; "" if never saved */
    char    *cur_text;               /* the model's project, as text           */
    bool     modified;

    /* undo: snapshots of the project, oldest first */
    PgProject *undo;
    int      undo_n, undo_pos;
    uint64_t change_ms;
    bool     change_waiting;

    /* action-bar message */
    char     msg[256];
    bool     msg_error;

    /* dialogs */
    PgDialog dialog;
    char     dlg_error[200];
    PgDialog dlg_measured;
    float    dlg_natural_h;

    /* wizard */
    int        wiz_step;
    PgProject  wiz;
    PgModel   *wiz_model;
    int        wiz_preset;             /* engine preset, or -1 for custom */

    /* file dialog */
    FileMode      file_mode;
    char          file_dir[PLAT_PATH_MAX];
    char          file_name[160];
    PlatDirEntry *entries;
    int           n_entries;
    bool          file_listed;

    /* unsaved-changes question */
    Pending  pending;
    char     pending_path[PLAT_PATH_MAX];
    bool     save_then_pending;

    /* design page */
    PgCamera    cam;
    PgViewOpts  vopt;
    bool        fit_pending;
    PgGlView   *glv;                   /* the GPU view, or NULL for software */
    PgMesh      mesh;                  /* what glv holds, and what it shows: */
    bool        mesh_ok, mesh_dark;
    unsigned    mesh_gen;
    PgViewOpts  mesh_vopt;
    PgRaster   *raster;
    uint8_t    *tex_pixels;
    SDL_Texture *tex;
    int         tex_w, tex_h, ss;
    bool        view_dirty;
    unsigned    view_gen;
    struct nk_rect view_rect;
    Inspector   insp;
    DragMode    drag;
    struct nk_vec2 drag_from;
    double      drag_bend, drag_roll;
    int         drag_joint;

    /* auto-fold, run a slice at a time from the frame loop */
    PgFoldOpts  fold;
    PgFolder   *folder;
    char       *fold_text;           /* the project, as text, when it began */
    char        fold_msg[480];
    bool        fold_ok;
    bool        fold_can_revert;
    PgRoute     fold_prev_route;
    int         fold_prev_segments;

    /* patterns page */
    int      part_sel;
    int      pattern_tab;              /* 0 part, 1 sheets */
    int      sheet_sel;

    /* export page */
    char     export_base[128];
    char     export_msg[512];
    bool     export_ok;

    /* tooltips: this frame's candidate, and the one the dwell counts on */
    char     tip_text[256];
    struct nk_rect tip_over;
    char     tip_shown[256];
    uint64_t tip_since_ms;

    char     title[320];
};

static inline float S(const PgUi *ui, float v) { return v * ui->scale; }

/* Leave one cell of the current row empty. Not nk_spacing(ctx, 1): that
 * wraps onto a new row when it fills the last column, and the next layout
 * call then advances by the row's height a second time — a one-column row
 * followed by nk_spacing silently takes twice its height. */
static inline void nk_skip(struct nk_context *c)
{
    struct nk_rect unused;
    nk_widget(&unused, c);
}

/* ---- pg_ui.c: helpers ---------------------------------------------- */

void  ui_tip(PgUi *ui, const char *text);
void  ui_tip_rect(PgUi *ui, struct nk_rect over, const char *text);
float ui_wrap_height(PgUi *ui, const char *text, float w);
void  ui_label_wrap(PgUi *ui, const char *text, struct nk_color col);
void  ui_form_row(PgUi *ui, float h);
void  ui_section(PgUi *ui, const char *title);
void  ui_gap(PgUi *ui, float h);
bool  ui_primary_button(PgUi *ui, const char *label);
void  ui_info_row(PgUi *ui, const char *k, const char *v);
void  ui_info_rowf(PgUi *ui, const char *k, const char *fmt, ...);
bool  ui_prop(PgUi *ui, const char *label, const char *id, const char *tip,
              double *v, double min, double max, double step, const char *unit);
bool  ui_prop_int(PgUi *ui, const char *label, const char *id, const char *tip,
                  int *v, int min, int max, const char *unit);
bool  ui_check(PgUi *ui, const char *label, const char *tip, bool *v);
void  ui_message(PgUi *ui, bool error, const char *fmt, ...);
PgDrawCtx ui_draw_ctx(PgUi *ui, bool mouse_ok);

/* ---- pg_ui.c: project actions -------------------------------------- */

void  ui_request_new(PgUi *ui);
void  ui_request_open(PgUi *ui);
void  ui_request_open_path(PgUi *ui, const char *path);
bool  ui_save(PgUi *ui, bool save_as);
bool  ui_save_to(PgUi *ui, const char *path);
bool  ui_open_path(PgUi *ui, const char *path);
void  ui_set_project(PgUi *ui, const PgProject *p, const char *path);
void  ui_undo(PgUi *ui);
void  ui_redo(PgUi *ui);
bool  ui_can_undo(const PgUi *ui);
bool  ui_can_redo(const PgUi *ui);
void  ui_continue_pending(PgUi *ui);

/* ---- pg_ui_design.c ------------------------------------------------ */

void  page_design(PgUi *ui, struct nk_rect r);
void  design_init(PgUi *ui);
void  design_shutdown(PgUi *ui);
bool  design_handle_key(PgUi *ui, SDL_Keycode key, Uint16 mod);
void  design_tick(PgUi *ui);          /* runs a slice of any auto-fold search */

/* ---- pg_ui_pages.c ------------------------------------------------- */

void  page_patterns(PgUi *ui, struct nk_rect r);
void  page_profile(PgUi *ui, struct nk_rect r);
void  page_export(PgUi *ui, struct nk_rect r);
void  page_help(PgUi *ui, struct nk_rect r);

/* The inspector's property forms (pg_ui_design.c), shared with the
 * wizard. Each returns
 * true if anything changed. */
bool  form_engine(PgUi *ui, PgProject *p, bool wizard);
bool  form_tuning(PgUi *ui, PgProject *p);
bool  form_chamber(PgUi *ui, PgProject *p);
bool  form_build(PgUi *ui, PgProject *p);

/* ---- pg_ui_dialogs.c ----------------------------------------------- */

void  ui_draw_dialog(PgUi *ui, int w, int h);
void  ui_open_wizard(PgUi *ui, const PgProject *seed);
void  ui_open_file_dialog(PgUi *ui, FileMode mode);
void  ui_open_confirm(PgUi *ui, Pending pending);

#endif /* PG_UI_INT_H */
