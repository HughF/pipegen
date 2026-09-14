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
#include "pg_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CONFIG_LEAF "settings.conf"

void pg_settings_default(PgSettings *s)
{
    memset(s, 0, sizeof *s);
    s->dark = true;

    char docs[PLAT_PATH_MAX];
    if (!plat_documents_dir(docs, sizeof docs))
        snprintf(docs, sizeof docs, ".");
    snprintf(s->project_dir, sizeof s->project_dir, "%s", docs);
    snprintf(s->export_dir, sizeof s->export_dir, "%s", docs);

    s->dxf_layout    = true;
    s->dxf_parts     = false;
    s->pdf_design    = true;
    s->pdf_layouts   = true;
    s->pdf_templates = true;
}

static bool config_path(char *buf, size_t cap)
{
    char dir[PLAT_PATH_MAX];
    if (!plat_config_dir(dir, sizeof dir))
        return false;
    return plat_path_join(buf, cap, dir, CONFIG_LEAF);
}

bool pg_settings_load(PgSettings *s)
{
    pg_settings_default(s);

    char path[PLAT_PATH_MAX];
    if (!config_path(path, sizeof path))
        return false;
    FILE *f = fopen(path, "r");
    if (!f)
        return false;

    char line[PLAT_PATH_MAX + 64];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        const char *k = line, *v = eq + 1;
        bool b = atoi(v) != 0;

        if (strcmp(k, "dark") == 0) s->dark = b;
        else if (strcmp(k, "last_project") == 0)
            snprintf(s->last_project, sizeof s->last_project, "%s", v);
        else if (strcmp(k, "project_dir") == 0 && *v)
            snprintf(s->project_dir, sizeof s->project_dir, "%s", v);
        else if (strcmp(k, "export_dir") == 0 && *v)
            snprintf(s->export_dir, sizeof s->export_dir, "%s", v);
        else if (strncmp(k, "recent", 6) == 0) {
            int i = atoi(k + 6);
            if (i >= 0 && i < PG_RECENT)
                snprintf(s->recent[i], sizeof s->recent[i], "%s", v);
        }
        else if (strcmp(k, "dxf_layout") == 0)    s->dxf_layout = b;
        else if (strcmp(k, "dxf_parts") == 0)     s->dxf_parts = b;
        else if (strcmp(k, "pdf_design") == 0)    s->pdf_design = b;
        else if (strcmp(k, "pdf_layouts") == 0)   s->pdf_layouts = b;
        else if (strcmp(k, "pdf_templates") == 0) s->pdf_templates = b;
    }
    fclose(f);
    return true;
}

bool pg_settings_save(const PgSettings *s)
{
    char path[PLAT_PATH_MAX];
    if (!config_path(path, sizeof path))
        return false;
    FILE *f = fopen(path, "w");
    if (!f)
        return false;

    fprintf(f, "dark=%d\n", s->dark ? 1 : 0);
    fprintf(f, "last_project=%s\n", s->last_project);
    fprintf(f, "project_dir=%s\n", s->project_dir);
    fprintf(f, "export_dir=%s\n", s->export_dir);
    for (int i = 0; i < PG_RECENT; i++)
        if (s->recent[i][0])
            fprintf(f, "recent%d=%s\n", i, s->recent[i]);
    fprintf(f, "dxf_layout=%d\n", s->dxf_layout ? 1 : 0);
    fprintf(f, "dxf_parts=%d\n", s->dxf_parts ? 1 : 0);
    fprintf(f, "pdf_design=%d\n", s->pdf_design ? 1 : 0);
    fprintf(f, "pdf_layouts=%d\n", s->pdf_layouts ? 1 : 0);
    fprintf(f, "pdf_templates=%d\n", s->pdf_templates ? 1 : 0);
    return fclose(f) == 0;
}

void pg_settings_add_recent(PgSettings *s, const char *path)
{
    if (!path || !path[0])
        return;

    char keep[PG_RECENT][PLAT_PATH_MAX];
    int n = 0;
    snprintf(keep[n++], PLAT_PATH_MAX, "%s", path);
    for (int i = 0; i < PG_RECENT && n < PG_RECENT; i++) {
        if (s->recent[i][0] && strcmp(s->recent[i], path) != 0)
            snprintf(keep[n++], PLAT_PATH_MAX, "%s", s->recent[i]);
    }
    for (int i = 0; i < PG_RECENT; i++) {
        if (i < n)
            memcpy(s->recent[i], keep[i], PLAT_PATH_MAX);
        else
            s->recent[i][0] = '\0';
    }
}
