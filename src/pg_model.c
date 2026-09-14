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
#include "pg_model.h"
#include "pg_dxf.h"
#include "pg_report.h"
#include "plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

PgModel *pg_model_new(void)
{
    PgModel *m = calloc(1, sizeof *m);
    if (!m)
        return NULL;
    m->chain = calloc(1, sizeof *m->chain);
    m->parts = calloc(1, sizeof *m->parts);
    m->layout = calloc(1, sizeof *m->layout);
    if (!m->chain || !m->parts || !m->layout) {
        pg_model_free(m);
        return NULL;
    }
    pg_project_default(&m->project);
    pg_model_update(m);
    return m;
}

void pg_model_free(PgModel *m)
{
    if (!m)
        return;
    free(m->chain);
    free(m->parts);
    free(m->layout);
    free(m);
}

void pg_model_update(PgModel *m)
{
    pg_design_compute(&m->project, &m->design);
    pg_chain_build(&m->project, &m->design, m->chain);
    pg_parts_build(&m->project, &m->design, m->chain, m->parts);
    pg_layout_build(m->parts, &m->project.build, m->layout);
    m->generation++;
}

void pg_export_base_name(const char *title, char *out, size_t cap)
{
    size_t n = 0;
    bool dash = false;
    for (const unsigned char *s = (const unsigned char *)title;
         *s && n + 1 < cap; s++) {
        if (isalnum(*s) || *s == '.' || *s == '_') {
            out[n++] = (char)tolower(*s);
            dash = false;
        } else if (!dash && n > 0) {
            out[n++] = '-';
            dash = true;
        }
    }
    while (n > 0 && out[n - 1] == '-')
        n--;
    out[n] = '\0';
    if (n == 0)
        snprintf(out, cap, "chamber");
}

bool pg_export_files(const PgModel *m, const char *dir, const char *base,
                     const PgExportOpts *o, char *msg, size_t msgcap)
{
    bool pdf = o->pdf_design || o->pdf_layouts || o->pdf_templates;
    if (!o->dxf_layout && !o->dxf_parts && !pdf) {
        snprintf(msg, msgcap, "Nothing chosen to export.");
        return false;
    }
    if (!dir[0] || !plat_mkdir(dir)) {
        snprintf(msg, msgcap, "Cannot use the folder %s.", dir[0] ? dir : "(none)");
        return false;
    }

    char safe[128];
    pg_export_base_name(base, safe, sizeof safe);

    char path[PLAT_PATH_MAX], leaf[200], err[256];
    char done[512] = "";
    int part_files = 0;

    if (o->dxf_layout) {
        snprintf(leaf, sizeof leaf, "%s.dxf", safe);
        if (!plat_path_join(path, sizeof path, dir, leaf) ||
            !pg_dxf_write_layout(path, &m->project, m->parts, m->layout,
                                 err, sizeof err)) {
            snprintf(msg, msgcap, "DXF: %s", err);
            return false;
        }
        snprintf(done + strlen(done), sizeof done - strlen(done), "%s", leaf);
    }

    if (o->dxf_parts) {
        for (int i = 0; i < m->parts->n; i++) {
            const PgPart *p = &m->parts->part[i];
            if (p->kind == PG_PART_TUBE)
                continue;
            snprintf(leaf, sizeof leaf, "%s-%s.dxf", safe, p->id);
            if (!plat_path_join(path, sizeof path, dir, leaf) ||
                !pg_dxf_write_part(path, &m->project, p, err, sizeof err)) {
                snprintf(msg, msgcap, "DXF %s: %s", p->id, err);
                return false;
            }
            part_files++;
        }
        snprintf(done + strlen(done), sizeof done - strlen(done),
                 "%s%d part DXF%s", done[0] ? ", " : "", part_files,
                 part_files == 1 ? "" : "s");
    }

    if (pdf) {
        PgReportOpts ro = { o->pdf_design, o->pdf_layouts, o->pdf_templates };
        snprintf(leaf, sizeof leaf, "%s.pdf", safe);
        if (!plat_path_join(path, sizeof path, dir, leaf) ||
            !pg_report_pdf(path, &m->project, &m->design, m->chain, m->parts,
                           m->layout,
                           &ro, err, sizeof err)) {
            snprintf(msg, msgcap, "PDF: %s", err);
            return false;
        }
        snprintf(done + strlen(done), sizeof done - strlen(done), "%s%s",
                 done[0] ? ", " : "", leaf);
    }

    snprintf(msg, msgcap, "Wrote %s to %s", done, dir);
    return true;
}
