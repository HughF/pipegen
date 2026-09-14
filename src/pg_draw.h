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
 * pg_draw.h — the two-dimensional drawings, on screen
 *
 * The side profile, the wave-timing plot, a flat pattern and a sheet of the
 * nesting layout, each drawn into a rectangle of a Nuklear command buffer.
 * The same figures the PDF carries, drawn with the screen theme, and each
 * reports what the pointer is over so the page can say more about it.
 */
#ifndef PG_DRAW_H
#define PG_DRAW_H

#include "nk.h"
#include "pg_theme.h"
#include "pg_project.h"
#include "pg_design.h"
#include "pg_route.h"
#include "pg_pattern.h"

typedef struct {
    struct nk_command_buffer   *cb;
    const struct nk_user_font  *font;
    const PgTheme              *theme;
    float                       scale;
    struct nk_vec2              mouse;     /* (-1,-1) when not hovering */
} PgDrawCtx;

/* Straightened side profile to scale (vertical exaggerated, and said so).
 * Returns the section under the pointer, or -1. */
int pg_draw_profile(const PgDrawCtx *dc, struct nk_rect r, const PgDesign *d,
                    const PgChain *c, int highlight_section);

/* Return angle of the suction and plugging waves against rpm. */
void pg_draw_timing(const PgDrawCtx *dc, struct nk_rect r, const PgProject *p,
                    const PgDesign *d);

/* One flat pattern, fitted to the rectangle, with its etch marks. */
void pg_draw_part(const PgDrawCtx *dc, struct nk_rect r, const PgPart *part,
                  bool marks);

/* One sheet of the layout. Returns the part under the pointer, or -1. */
int pg_draw_sheet(const PgDrawCtx *dc, struct nk_rect r, const PgPartList *parts,
                  const PgLayout *lay, int sheet, int highlight_part);

#endif /* PG_DRAW_H */
