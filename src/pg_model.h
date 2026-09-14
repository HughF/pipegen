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
 * pg_model.h — a project and everything derived from it
 *
 * The inputs, the chamber, its route through space, the flat patterns and
 * their layout, kept together and rebuilt together, so nothing on screen or
 * on disk can be out of step with the rest. Also the one place that writes
 * the export files, which both the Export page and the headless --export
 * option call.
 */
#ifndef PG_MODEL_H
#define PG_MODEL_H

#include <stdbool.h>
#include <stddef.h>

#include "pg_project.h"
#include "pg_design.h"
#include "pg_route.h"
#include "pg_pattern.h"

typedef struct {
    PgProject   project;
    PgDesign    design;
    PgChain    *chain;       /* heap: large */
    PgPartList *parts;       /* heap: large */
    PgLayout   *layout;      /* heap: large */
    unsigned    generation;  /* bumped by every update                   */
} PgModel;

PgModel *pg_model_new(void);
void     pg_model_free(PgModel *m);

/* Recompute everything from m->project. */
void pg_model_update(PgModel *m);

typedef struct {
    bool dxf_layout;       /* <base>.dxf: every sheet                     */
    bool dxf_parts;        /* <base>-P01.dxf ...: one file per part       */
    bool pdf_design;       /* <base>.pdf: design sheet, timing, cut list  */
    bool pdf_layouts;      /*            + sheet layouts                  */
    bool pdf_templates;    /*            + full-size templates            */
} PgExportOpts;

/* Write the chosen files into `dir` (created if missing). On success `msg`
 * lists what was written; on failure, why not. */
bool pg_export_files(const PgModel *m, const char *dir, const char *base,
                     const PgExportOpts *o, char *msg, size_t msgcap);

/* A project title made safe as a file name. */
void pg_export_base_name(const char *title, char *out, size_t cap);

#endif /* PG_MODEL_H */
