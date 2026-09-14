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
 * pg_pdf.h — a small PDF writer
 *
 * Enough PDF for drawings: pages of any size, lines, Bézier curves, filled
 * shapes, dashes and text in the two standard Helvetica faces, which every
 * PDF reader carries, so nothing is embedded and there is no dependency.
 * Coordinates are millimetres from the bottom-left corner of the page, which
 * is what makes a 1:1 template come out at true size.
 */
#ifndef PG_PDF_H
#define PG_PDF_H

#include <stdbool.h>
#include <stddef.h>

#include "pg_geom.h"

typedef struct PgPdf PgPdf;

typedef enum { PG_PDF_REGULAR = 0, PG_PDF_BOLD } PgPdfFont;
typedef enum { PG_PDF_LEFT = 0, PG_PDF_CENTRE, PG_PDF_RIGHT } PgPdfAlign;

/* Uniform scale about the origin, then rotation, then translation — the only
 * transform under which an arc stays an arc. */
typedef struct {
    double scale;
    double rot;          /* radians */
    double dx, dy;       /* page mm */
} PgPdfXf;

PgPdf *pg_pdf_new(void);
void   pg_pdf_free(PgPdf *p);

void pg_pdf_page(PgPdf *p, double w_mm, double h_mm);

void pg_pdf_stroke_rgb(PgPdf *p, int r, int g, int b);
void pg_pdf_fill_rgb(PgPdf *p, int r, int g, int b);
void pg_pdf_line_width(PgPdf *p, double mm);
void pg_pdf_dash(PgPdf *p, double on_mm, double off_mm);   /* 0,0 = solid */

void pg_pdf_move(PgPdf *p, double x, double y);
void pg_pdf_line(PgPdf *p, double x, double y);
void pg_pdf_curve(PgPdf *p, double x1, double y1, double x2, double y2,
                  double x3, double y3);
void pg_pdf_close(PgPdf *p);
void pg_pdf_stroke(PgPdf *p);
void pg_pdf_fill(PgPdf *p);
void pg_pdf_fill_stroke(PgPdf *p);

void pg_pdf_rect(PgPdf *p, double x, double y, double w, double h);
void pg_pdf_segment(PgPdf *p, double x0, double y0, double x1, double y1);

/* Append a bulge polyline to the current path, arcs as Béziers. */
void pg_pdf_outline(PgPdf *p, const PgVert *v, int n, bool closed,
                    const PgPdfXf *xf);

/* Place an RGBA image (alpha ignored) in the rectangle x, y, w, h (mm).
 * Stored run-length encoded, which every reader decodes and needs no zlib. */
void pg_pdf_image(PgPdf *p, double x, double y, double w, double h,
                  const unsigned char *rgba, int iw, int ih);

void pg_pdf_text(PgPdf *p, double x, double y, double size_pt, PgPdfFont font,
                 PgPdfAlign align, double rot_deg, const char *utf8);

/* Width of `utf8` set at size_pt, in mm. */
double pg_pdf_text_width(double size_pt, PgPdfFont font, const char *utf8);

bool pg_pdf_save(PgPdf *p, const char *path, const char *title,
                 char *err, size_t errcap);

#endif /* PG_PDF_H */
