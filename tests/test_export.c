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
 * test_export.c — the files are well formed.
 *
 * A DXF with a miscounted section or a PDF with one wrong xref offset opens
 * in some programs and not others, which is the worst way to find out.
 */
#include "pg_test.h"
#include "pg_model.h"
#include "pg_dxf.h"
#include "pg_report.h"

#include <stdlib.h>

static size_t file_len;     /* of the last file slurped */

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = '\0';
        *len = (size_t)n;
        file_len = (size_t)n;
    }
    return buf;
}

/* Binary-safe: a PDF with an embedded image has zero bytes in it, and
 * strstr stops at the first. */
static int count(const char *hay, const char *needle)
{
    size_t m = strlen(needle), n = file_len ? file_len : strlen(hay);
    int c = 0;
    for (size_t i = 0; i + m <= n; i++)
        if (hay[i] == needle[0] && memcmp(hay + i, needle, m) == 0) {
            c++;
            i += m - 1;
        }
    return c;
}

static const char *find_last(const char *hay, size_t n, const char *needle)
{
    size_t m = strlen(needle);
    for (size_t i = n >= m ? n - m + 1 : 0; i-- > 0;)
        if (memcmp(hay + i, needle, m) == 0)
            return hay + i;
    return NULL;
}

static void test_dxf(PgModel *x)
{
    char err[200] = "";
    const char *path = "build/testout/layout.dxf";
    CHECK(pg_dxf_write_layout(path, &x->project, x->parts, x->layout, err, sizeof err));
    CHECK_STR(err, "");

    size_t len = 0;
    char *s = slurp(path, &len);
    CHECK(s != NULL);
    if (!s)
        return;

    CHECK(strstr(s, "AC1009") != NULL);
    CHECK(count(s, "\nSECTION\n") == 4);
    CHECK(count(s, "\nENDSEC\n") == 4);
    CHECK(len > 4 && strcmp(s + len - 4, "EOF\n") == 0);

    /* one polyline per placed part, one per sheet border */
    CHECK(count(s, "\nPOLYLINE\n") == x->layout->n + x->layout->n_sheets);
    CHECK(count(s, "\nPOLYLINE\n") == count(s, "\nSEQEND\n"));
    /* arcs go out as bulges, wherever there is a cone */
    const PgPart *sheet_part = NULL, *tube = NULL;
    bool has_cone = false;
    for (int i = 0; i < x->parts->n; i++) {
        const PgPart *p = &x->parts->part[i];
        if (p->kind == PG_PART_CONE)
            has_cone = true;
        if (p->kind == PG_PART_TUBE && !tube)
            tube = p;
        if (p->kind != PG_PART_TUBE && !sheet_part)
            sheet_part = p;
    }
    if (has_cone)
        CHECK(count(s, "\n 42\n") > 0);
    free(s);

    /* one part */
    CHECK(sheet_part != NULL);
    if (sheet_part) {
        CHECK(pg_dxf_write_part("build/testout/part.dxf", &x->project,
                                sheet_part, err, sizeof err));
        s = slurp("build/testout/part.dxf", &len);
        CHECK(s && count(s, "\nPOLYLINE\n") == 1);
        free(s);
    }

    /* a tube has no flat pattern, and says so */
    if (tube) {
        err[0] = '\0';
        CHECK(!pg_dxf_write_part("build/testout/tube.dxf", &x->project,
                                 tube, err, sizeof err));
        CHECK(strstr(err, "tube") != NULL);
    }
}

/* Every xref entry must point at the start of its object. */
static void test_pdf(PgModel *x)
{
    char err[200] = "";
    const char *path = "build/testout/report.pdf";
    PgReportOpts o = { true, true, true };
    CHECK(pg_report_pdf(path, &x->project, &x->design, x->chain, x->parts,
                        x->layout, &o, err, sizeof err));
    CHECK_STR(err, "");

    size_t len = 0;
    char *s = slurp(path, &len);
    CHECK(s != NULL);
    if (!s)
        return;

    CHECK(strncmp(s, "%PDF-1.4", 8) == 0);
    CHECK(len > 6 && strcmp(s + len - 6, "%%EOF\n") == 0);

    const char *sx = find_last(s, len, "startxref\n");
    CHECK(sx != NULL);
    if (sx) {
        long xref = atol(sx + 10);
        CHECK(strncmp(s + xref, "xref\n0 ", 7) == 0);
        int nobj = atoi(s + xref + 7);
        CHECK(nobj > 5);
        const char *e = strchr(s + xref + 5, '\n') + 1 + 20;   /* skip entry 0 */
        int good = 0;
        for (int i = 1; i < nobj; i++, e += 20) {
            long off = atol(e);
            char want[24];
            snprintf(want, sizeof want, "%d 0 obj", i);
            if (off > 0 && (size_t)off < len &&
                strncmp(s + off, want, strlen(want)) == 0)
                good++;
        }
        CHECK(good == nobj - 1);
    }

    int sheet_parts = 0;
    for (int i = 0; i < x->parts->n; i++)
        if (x->parts->part[i].kind != PG_PART_TUBE)
            sheet_parts++;
    /* design sheet + 3D and route + timing/cut list + sheets + one
     * template per part */
    CHECK(count(s, "/Type /Page ") == 3 + x->layout->n_sheets + sheet_parts);
    /* the rendered 3D view, embedded and compressed */
    CHECK(count(s, "/Subtype /Image") == 1);
    CHECK(count(s, "/Filter /RunLengthDecode") == 1);
    free(s);
}

static void test_export_all(PgModel *x)
{
    PgExportOpts o = {
        .dxf_layout = true, .dxf_parts = true,
        .pdf_design = true, .pdf_layouts = true, .pdf_templates = true,
    };
    char msg[512] = "";
    CHECK(pg_export_files(x, "build/testout", "all", &o, msg, sizeof msg));
    FILE *f = fopen("build/testout/all.dxf", "r");
    CHECK(f != NULL);
    if (f) fclose(f);
    f = fopen("build/testout/all.pdf", "r");
    CHECK(f != NULL);
    if (f) fclose(f);
    f = fopen("build/testout/all-P01.dxf", "r");
    CHECK(f != NULL);
    if (f) fclose(f);

    /* nothing chosen is an error, not silence */
    PgExportOpts none = { 0 };
    CHECK(!pg_export_files(x, "build/testout", "none", &none, msg, sizeof msg));
}

int main(void)
{
    printf("test_export\n");

    PgModel *x = pg_model_new();
    CHECK(x != NULL);
    if (!x)
        return 1;
    pg_project_default(&x->project);
    pg_model_update(x);
    test_dxf(x);
    test_pdf(x);
    test_export_all(x);

    /* bent, which puts mitres in the cut list and the route page */
    pg_route_set_bend(&x->project.route, x->chain->joint[4].x, 1.0, 35.0, 90.0);
    pg_model_update(x);
    test_dxf(x);
    test_pdf(x);

    /* and the hydroformed method, which writes different outlines */
    x->project.build.method = PG_MFG_HYDRO;
    pg_model_update(x);
    test_dxf(x);
    test_pdf(x);

    pg_model_free(x);
    printf("  %d checks, %d failed\n", pg_test_count, pg_test_fails);
    return pg_test_fails ? 1 : 0;
}
