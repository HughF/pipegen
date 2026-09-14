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
 * pg_config.h — per-user settings, remembered between runs
 *
 * Not the design: that lives in the project file. This is where the program
 * was left — theme, folders, the recent-projects list, export choices.
 */
#ifndef PG_CONFIG_H
#define PG_CONFIG_H

#include <stdbool.h>
#include "plat.h"

#define PG_RECENT 6

typedef struct {
    bool dark;
    char last_project[PLAT_PATH_MAX];
    char project_dir[PLAT_PATH_MAX];
    char export_dir[PLAT_PATH_MAX];
    char recent[PG_RECENT][PLAT_PATH_MAX];

    bool dxf_layout;       /* all sheets in one DXF          */
    bool dxf_parts;        /* one DXF per part               */
    bool pdf_design;       /* design sheet + cut list        */
    bool pdf_layouts;      /* scaled sheet layouts           */
    bool pdf_templates;    /* full-size part templates       */
} PgSettings;

void pg_settings_default(PgSettings *s);
bool pg_settings_load(PgSettings *s);
bool pg_settings_save(const PgSettings *s);

/* Move `path` to the top of the recent list. */
void pg_settings_add_recent(PgSettings *s, const char *path);

#endif /* PG_CONFIG_H */
