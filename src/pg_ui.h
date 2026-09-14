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
#ifndef PG_UI_H
#define PG_UI_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct PgUi PgUi;

typedef struct {
    const char *project;     /* open this project at startup, or NULL      */
    bool        wizard;      /* start in the New Project wizard            */
} PgUiStart;

struct PgGlView;

/* What the interface draws with: an SDL_Renderer (the software path), or the
 * window's current OpenGL 3.3 context and the 3D view made in it. */
typedef struct {
    SDL_Window      *win;
    SDL_Renderer    *ren;        /* NULL when drawing with OpenGL              */
    struct PgGlView *glview;     /* OpenGL only; the interface takes ownership */
    const char      *renderer;   /* for the About box, e.g. the GL renderer    */
} PgUiVideo;

/* NULL on failure. On the OpenGL path the caller can then fall back to an
 * SDL_Renderer; the 3D view has been freed either way. */
PgUi *pg_ui_create(const PgUiVideo *video, const PgUiStart *start);
void  pg_ui_destroy(PgUi *ui);

void  pg_ui_fit_window(PgUi *ui, int base_w, int base_h);

void  pg_ui_input_begin(PgUi *ui);
void  pg_ui_input_end(PgUi *ui);
bool  pg_ui_handle_event(PgUi *ui, SDL_Event *e);

/* The drawable's size in pixels, which the frame is laid out in. */
void  pg_ui_output_size(const PgUi *ui, int *w, int *h);
void  pg_ui_frame(PgUi *ui, int win_w, int win_h);

/* Clear, draw the frame and show it. */
void  pg_ui_present(PgUi *ui);

bool  pg_ui_quit_requested(const PgUi *ui);

/* True while work is running in the background of the frame loop (an
 * auto-fold search), so the loop should not sleep waiting for input. */
bool  pg_ui_busy(const PgUi *ui);

#endif /* PG_UI_H */
