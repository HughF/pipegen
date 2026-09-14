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
 * pg_dxf.h — laser-cutting output
 *
 * AutoCAD R12 ASCII (AC1009): the oldest DXF dialect, and the one every laser
 * CAM package, nesting program and CAD viewer still reads. Millimetres, one
 * closed POLYLINE per part with arcs kept as true arcs (vertex bulges), and
 * four layers so the operator can switch off what must not be cut:
 *
 *   CUT    part outlines                     colour 7
 *   ETCH   alignment marks — mark, do not cut colour 5
 *   LABEL  part numbers — engrave or ignore  colour 3
 *   SHEET  sheet borders and titles          colour 8
 */
#ifndef PG_DXF_H
#define PG_DXF_H

#include <stdbool.h>
#include <stddef.h>

#include "pg_pattern.h"

/* Every sheet of the layout, side by side, in one file. */
bool pg_dxf_write_layout(const char *path, const PgProject *pr,
                         const PgPartList *parts, const PgLayout *lay,
                         char *err, size_t errcap);

/* One part at the origin, unrotated, in its own file. */
bool pg_dxf_write_part(const char *path, const PgProject *pr,
                       const PgPart *part, char *err, size_t errcap);

#endif /* PG_DXF_H */
