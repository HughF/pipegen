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

PgUi *pg_ui_create(SDL_Window *win, SDL_Renderer *ren, const PgUiStart *start);
void  pg_ui_destroy(PgUi *ui);

void  pg_ui_fit_window(PgUi *ui, int base_w, int base_h);

void  pg_ui_input_begin(PgUi *ui);
void  pg_ui_input_end(PgUi *ui);
bool  pg_ui_handle_event(PgUi *ui, SDL_Event *e);

void  pg_ui_frame(PgUi *ui, int win_w, int win_h);
void  pg_ui_render(PgUi *ui);

void  pg_ui_clear_colour(const PgUi *ui, Uint8 *r, Uint8 *g, Uint8 *b);
bool  pg_ui_quit_requested(const PgUi *ui);

/* True while work is running in the background of the frame loop (an
 * auto-fold search), so the loop should not sleep waiting for input. */
bool  pg_ui_busy(const PgUi *ui);

#endif /* PG_UI_H */
