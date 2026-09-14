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
 * pg_main.c — entry point, command line and frame loop
 *
 *   pipegen [--project FILE] [--wizard]
 *   pipegen [--project FILE] [--autofold compact|box] [--save FILE]
 *           [--export DIR NAME] [--render FILE.ppm [W H]]
 *
 * The headless options never open a window: they run the same model, search
 * and writers the program uses, so a design can be folded and exported from a
 * script, and the 3D view can be checked without a display. They run in the
 * order written above.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pg_ui.h"
#include "pg_gl.h"
#include "pg_glview.h"
#include "pg_model.h"
#include "pg_view3d.h"
#include "pg_autofold.h"
#include "pg_help.h"
#include "pg_version.h"

#define BASE_W 1440
#define BASE_H  900

/* A -mwindows program has no console; reattach to the one it was started
 * from, so the command-line options can print. */
static void console(void)
{
#ifdef _WIN32
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
#endif
}

static SDL_Window *make_window(Uint32 flags)
{
    return SDL_CreateWindow(PIPEGEN_NAME " " PIPEGEN_VERSION,
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, BASE_W, BASE_H,
                            SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE |
                            SDL_WINDOW_ALLOW_HIGHDPI | flags);
}

/*
 * The interface and the 3D view drawn with OpenGL 3.3. NULL, with the reason
 * on stderr and nothing left open, if this machine cannot — a virtual machine
 * without 3D, a Remote Desktop session, an old driver — and the program then
 * draws in software as it always could.
 */
static PgUi *start_gl(const PgUiStart *start, SDL_Window **win_out, SDL_GLContext *glc_out)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);    /* the view has its own */

    char why[512] = "";
    PgUi *ui = NULL;
    SDL_Window *win = make_window(SDL_WINDOW_OPENGL);
    SDL_GLContext glc = win ? SDL_GL_CreateContext(win) : NULL;
    if (!win || !glc) {
        snprintf(why, sizeof why, "%s", SDL_GetError());
    } else if (pg_gl_load(why, sizeof why)) {
        PgGlView *view = pg_glview_new(why, sizeof why);
        if (view) {
            char desc[200];
            pg_gl_describe(desc, sizeof desc);
            SDL_GL_SetSwapInterval(1);
            PgUiVideo video = { .win = win, .glview = view, .renderer = desc };
            ui = pg_ui_create(&video, start);
            if (!ui)
                snprintf(why, sizeof why, "the interface could not start");
        }
    }
    if (ui) {
        *win_out = win;
        *glc_out = glc;
        return ui;
    }
    fprintf(stderr, "pipegen: no OpenGL 3.3 (%s); drawing in software\n", why);
    if (glc)
        SDL_GL_DeleteContext(glc);
    if (win)
        SDL_DestroyWindow(win);
    SDL_GL_ResetAttributes();   /* SDL's own GL renderer wants its defaults */
    return NULL;
}

static PgUi *start_software(const PgUiStart *start, SDL_Window **win_out,
                            SDL_Renderer **ren_out)
{
    SDL_Window *win = make_window(0);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return NULL;
    }
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        return NULL;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    char desc[200] = "software 3D view";
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(ren, &info) == 0)
        snprintf(desc, sizeof desc, "software 3D view, SDL %s renderer", info.name);
    PgUiVideo video = { .win = win, .ren = ren, .renderer = desc };
    PgUi *ui = pg_ui_create(&video, start);
    if (!ui) {
        fprintf(stderr, "pipegen: cannot create the interface\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return NULL;
    }
    *win_out = win;
    *ren_out = ren;
    return ui;
}

static void usage(void)
{
    printf("%s %s — %s\n\n", PIPEGEN_NAME, PIPEGEN_VERSION, PIPEGEN_TAGLINE);
    printf("  pipegen [options]\n\n"
           "  --project FILE        open this project (.pgp)\n"
           "  --wizard              start in the New Project wizard\n\n"
           "Without a window, in this order:\n"
           "  --autofold GOAL       fold the chamber safely: GOAL is compact, or\n"
           "                        box (fit the project's clearance box)\n"
           "  --save FILE           write the project (after any fold)\n"
           "  --export DIR NAME     write NAME.dxf, NAME-P01.dxf... and NAME.pdf\n"
           "  --render FILE [W H]   render the 3D view to a PPM image\n\n"
           "  --save-default FILE   write the default JLO L372 project, then exit\n"
           "  --help                this message\n"
           "  --help-doc            write the built-in manual as Markdown\n\n"
           "Environment:\n"
           "  PIPEGEN_SCALE         override the HiDPI UI scale (e.g. 2)\n");
}

static PgModel *load_model(const char *project)
{
    PgModel *m = pg_model_new();
    if (!m) {
        fprintf(stderr, "pipegen: out of memory\n");
        return NULL;
    }
    if (project) {
        char err[256] = "";
        if (!pg_project_load(&m->project, project, err, sizeof err)) {
            fprintf(stderr, "pipegen: %s\n", err);
            pg_model_free(m);
            return NULL;
        }
        if (err[0])
            fprintf(stderr, "pipegen: %s\n", err);
        pg_model_update(m);
    }
    return m;
}

static int run_fold(PgModel *m, const char *goal)
{
    PgFoldOpts o;
    pg_fold_defaults(&o, &m->project);
    if (strcmp(goal, "compact") == 0) {
        o.goal = PG_FOLD_COMPACT;
    } else if (strcmp(goal, "box") == 0) {
        o.goal = PG_FOLD_FIT_BOX;
    } else {
        fprintf(stderr, "pipegen: --autofold wants compact or box, not '%s'\n", goal);
        return 2;
    }
    PgFoldResult r;
    bool ok = pg_autofold(&m->project, &o, &r);
    printf("%s\n", r.summary);
    if (!ok)
        return 1;
    pg_fold_apply(&m->project, &r);
    pg_model_update(m);
    return 0;
}

static int run_export(PgModel *m, const char *dir, const char *name)
{
    PgExportOpts o = { true, true, true, true, true };
    char msg[512];
    bool ok = pg_export_files(m, dir, name, &o, msg, sizeof msg);
    printf("%s\n", msg);
    for (int i = 0; i < m->design.n_warn; i++)
        printf("check: %s\n", m->design.warn[i]);
    for (int i = 0; i < m->chain->n_warn; i++)
        printf("check: %s\n", m->chain->warn[i]);
    for (int i = 0; i < m->parts->n_warn; i++)
        printf("check: %s\n", m->parts->warn[i]);
    return ok ? 0 : 1;
}

static int run_render(PgModel *m, const char *out, int w, int h)
{
    PgRaster *r = pg_raster_new(w * 2, h * 2);
    unsigned char *px = malloc((size_t)w * (size_t)h * 4);
    if (!r || !px) {
        fprintf(stderr, "pipegen: out of memory\n");
        pg_raster_free(r);
        free(px);
        return 1;
    }
    PgCamera cam;
    pg_camera_default(&cam);
    pg_camera_fit(&cam, m->chain, true, (double)w / h);
    PgViewOpts o = { .colour = PG_COLOUR_SECTION, .show_engine = true,
                     .show_box = true, .show_seams = true, .show_grid = true,
                     .hover_id = -1, .select_id = -1, .highlight_section = -1 };
    pg_view3d_render(r, &cam, &m->project, m->chain, &o, &PG_VIEW_DARK);
    pg_raster_downsample(r, 2, px, w, h);

    int rc = 0;
    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "pipegen: cannot write %s\n", out);
        rc = 1;
    } else {
        fprintf(f, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++)
            fwrite(px + i * 4, 1, 3, f);
        fclose(f);
        printf("rendered %s (%d x %d)\n", out, w, h);
    }
    free(px);
    pg_raster_free(r);
    return rc;
}

int main(int argc, char **argv)
{
    PgUiStart start = { NULL, false };
    const char *export_dir = NULL, *export_name = NULL;
    const char *render = NULL, *fold = NULL, *save = NULL;
    int render_w = 1280, render_h = 800;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            start.project = argv[++i];
        } else if (strcmp(argv[i], "--wizard") == 0) {
            start.wizard = true;
        } else if (strcmp(argv[i], "--autofold") == 0 && i + 1 < argc) {
            fold = argv[++i];
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            save = argv[++i];
        } else if (strcmp(argv[i], "--export") == 0 && i + 2 < argc) {
            export_dir = argv[++i];
            export_name = argv[++i];
        } else if (strcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            render = argv[++i];
            if (i + 2 < argc && atoi(argv[i + 1]) > 0 && atoi(argv[i + 2]) > 0) {
                render_w = atoi(argv[++i]);
                render_h = atoi(argv[++i]);
            }
        } else if (strcmp(argv[i], "--save-default") == 0 && i + 1 < argc) {
            console();
            PgProject p;
            pg_project_default(&p);
            char err[256];
            if (!pg_project_save(&p, argv[++i], err, sizeof err)) {
                fprintf(stderr, "pipegen: %s\n", err);
                return 1;
            }
            printf("wrote %s\n", argv[i]);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            console();
            usage();
            return 0;
        } else if (strcmp(argv[i], "--help-doc") == 0) {
            console();
            pg_help_write_markdown(stdout, PIPEGEN_VERSION);
            return 0;
        } else {
            console();
            fprintf(stderr, "pipegen: unknown argument '%s'\n", argv[i]);
            usage();
            return 2;
        }
    }

    if (fold || save || export_dir || render) {
        console();
        PgModel *m = load_model(start.project);
        if (!m)
            return 1;
        int rc = 0;
        if (fold)
            rc = run_fold(m, fold);
        if (rc == 0 && save) {
            char err[256];
            if (pg_project_save(&m->project, save, err, sizeof err)) {
                printf("wrote %s\n", save);
            } else {
                fprintf(stderr, "pipegen: %s\n", err);
                rc = 1;
            }
        }
        if (rc == 0 && export_dir)
            rc = run_export(m, export_dir, export_name);
        if (rc == 0 && render)
            rc = run_render(m, render, render_w, render_h);
        pg_model_free(m);
        return rc;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    SDL_Window *win = NULL;
    SDL_Renderer *ren = NULL;
    SDL_GLContext glc = NULL;
    PgUi *ui = NULL;
    const char *choice = getenv("PIPEGEN_RENDERER");
    if (!choice || strcmp(choice, "software") != 0)
        ui = start_gl(&start, &win, &glc);
    if (!ui)
        ui = start_software(&start, &win, &ren);
    if (!ui) {
        SDL_Quit();
        return 1;
    }

    pg_ui_fit_window(ui, BASE_W, BASE_H);
    SDL_ShowWindow(win);

    while (!pg_ui_quit_requested(ui)) {
        SDL_Event e;
        pg_ui_input_begin(ui);
        /* Nothing moves on its own, so sleep until the operator does
         * something — unless a search is running in the frame loop, which
         * needs every frame it can get. */
        bool got = pg_ui_busy(ui) ? SDL_PollEvent(&e) != 0
                                  : SDL_WaitEventTimeout(&e, 250) != 0;
        if (got) {
            pg_ui_handle_event(ui, &e);
            while (SDL_PollEvent(&e))
                pg_ui_handle_event(ui, &e);
        }
        pg_ui_input_end(ui);

        int w = 0, h = 0;
        pg_ui_output_size(ui, &w, &h);
        pg_ui_frame(ui, w, h);
        pg_ui_present(ui);
    }

    pg_ui_destroy(ui);                  /* while its GL context still exists */
    if (ren)
        SDL_DestroyRenderer(ren);
    if (glc)
        SDL_GL_DeleteContext(glc);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
