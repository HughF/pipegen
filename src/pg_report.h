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
 * pg_report.h — the PDF a workshop works from
 *
 *   1. Design sheet: side profile, section table, key figures, warnings.
 *   2. The chamber in 3D with the engine, its envelope, clashes, and the
 *      bend schedule with every mitre.
 *   3. Wave timing and the cut list, with the tube cut list and material.
 *   4. One page per sheet of the nesting layout, scaled to fit.
 *   5. One page per part at full size, with a 100 mm check bar, for
 *      printing on a plotter or checking a laser's output against.
 */
#ifndef PG_REPORT_H
#define PG_REPORT_H

#include <stdbool.h>
#include <stddef.h>

#include "pg_project.h"
#include "pg_design.h"
#include "pg_route.h"
#include "pg_pattern.h"

typedef struct {
    bool design;       /* pages 1 to 3 */
    bool layouts;
    bool templates;
} PgReportOpts;

bool pg_report_pdf(const char *path, const PgProject *pr, const PgDesign *d,
                   const PgChain *chain, const PgPartList *parts,
                   const PgLayout *lay,
                   const PgReportOpts *opt, char *err, size_t errcap);

#endif /* PG_REPORT_H */
