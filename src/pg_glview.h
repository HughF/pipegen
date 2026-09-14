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
 * pg_glview.h — the 3D design view drawn by the GPU
 *
 * The scene mesh from pg_view3d is uploaded once per change and kept on the
 * GPU; moving the camera only redraws it. The view is drawn, multisampled,
 * into a texture that the interface places like any other image, so dialogs
 * and tooltips stay on top of it.
 *
 * Picking draws the same mesh again with each surface's id as its colour and
 * reads back the one pixel under the pointer — exact, like the software
 * rasteriser's id buffer, and only redrawn after the view has changed.
 *
 * Needs a current OpenGL 3.3 context and pg_gl_load().
 */
#ifndef PG_GLVIEW_H
#define PG_GLVIEW_H

#include <stdbool.h>
#include <stddef.h>

#include "pg_view3d.h"

typedef struct PgGlView PgGlView;

PgGlView *pg_glview_new(char *err, size_t cap);
void      pg_glview_free(PgGlView *v);

/* Replace the scene. */
bool      pg_glview_upload(PgGlView *v, const PgMesh *m);

/* Draw the scene at w x h. line_scale multiplies every line width (the mesh
 * gives them in supersampled pixels; see pg_view3d.h). */
bool      pg_glview_render(PgGlView *v, const PgCamera *cam, int w, int h,
                           const PgViewStyle *st, float line_scale);

/* The texture holding the last render, top row first. */
unsigned  pg_glview_texture(const PgGlView *v);

/* The PG_PICK id at pixel (x, y) from the top left of the last render. */
int       pg_glview_pick(PgGlView *v, int x, int y);

/* Multisample count in use, 0 for none. */
int       pg_glview_samples(const PgGlView *v);

#endif /* PG_GLVIEW_H */
