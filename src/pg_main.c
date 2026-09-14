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
 *   pipegen [--project FILE] --export DIR NAME
 *   pipegen [--project FILE] --render FILE.ppm [W H]
 *
 * The headless options never open a window: they run the same model and
 * writers the program uses, so a design can be exported from a script, and
 * the 3D view can be checked without a display.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "pg_ui.h"
#include "pg_model.h"
#include "pg_view3d.h"
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

static void usage(void)
{
    printf("%s %s — %s\n\n", PIPEGEN_NAME, PIPEGEN_VERSION, PIPEGEN_TAGLINE);
    printf("  pipegen [options]\n\n"
           "  --project FILE        open this project (.pgp)\n"
           "  --wizard              start in the New Project wizard\n"
           "  --export DIR NAME     write NAME.dxf, NAME-P01.dxf... and NAME.pdf\n"
           "                        into DIR, then exit (no window)\n"
           "  --render FILE [W H]   render the 3D view to a PPM image, then exit\n"
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

static int run_export(const char *project, const char *dir, const char *name)
{
    PgModel *m = load_model(project);
    if (!m)
        return 1;
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
    pg_model_free(m);
    return ok ? 0 : 1;
}

static int run_render(const char *project, const char *out, int w, int h)
{
    PgModel *m = load_model(project);
    if (!m)
        return 1;
    PgRaster *r = pg_raster_new(w * 2, h * 2);
    unsigned char *px = malloc((size_t)w * (size_t)h * 4);
    if (!r || !px) {
        fprintf(stderr, "pipegen: out of memory\n");
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

    FILE *f = fopen(out, "wb");
    if (!f) {
        fprintf(stderr, "pipegen: cannot write %s\n", out);
        return 1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++)
        fwrite(px + i * 4, 1, 3, f);
    fclose(f);
    printf("rendered %s (%d x %d)\n", out, w, h);
    free(px);
    pg_raster_free(r);
    pg_model_free(m);
    return 0;
}

int main(int argc, char **argv)
{
    PgUiStart start = { NULL, false };
    const char *export_dir = NULL, *export_name = NULL;
    const char *render = NULL;
    int render_w = 1280, render_h = 800;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--project") == 0 && i + 1 < argc) {
            start.project = argv[++i];
        } else if (strcmp(argv[i], "--wizard") == 0) {
            start.wizard = true;
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

    if (export_dir) {
        console();
        return run_export(start.project, export_dir, export_name);
    }
    if (render) {
        console();
        return run_render(start.project, render, render_w, render_h);
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    SDL_Window *win = SDL_CreateWindow(
        PIPEGEN_NAME " " PIPEGEN_VERSION,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, BASE_W, BASE_H,
        SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);

    PgUi *ui = pg_ui_create(win, ren, &start);
    if (!ui) {
        fprintf(stderr, "pipegen: cannot create the interface\n");
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    pg_ui_fit_window(ui, BASE_W, BASE_H);
    SDL_ShowWindow(win);

    while (!pg_ui_quit_requested(ui)) {
        SDL_Event e;
        pg_ui_input_begin(ui);
        /* Nothing here moves on its own: sleep until the operator does
         * something, rather than redrawing the same frame sixty times a
         * second. */
        if (SDL_WaitEventTimeout(&e, 250)) {
            pg_ui_handle_event(ui, &e);
            while (SDL_PollEvent(&e))
                pg_ui_handle_event(ui, &e);
        }
        pg_ui_input_end(ui);

        int w = 0, h = 0;
        SDL_GetRendererOutputSize(ren, &w, &h);
        pg_ui_frame(ui, w, h);

        Uint8 r, g, b;
        pg_ui_clear_colour(ui, &r, &g, &b);
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderClear(ren);
        pg_ui_render(ui);
        SDL_RenderPresent(ren);
    }

    pg_ui_destroy(ui);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
