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
#include "pg_pdf.h"
#include "pg_version.h"
#include "plat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#define PT_PER_MM (72.0 / 25.4)

typedef struct {
    char  *data;
    size_t len, cap;
    bool   oom;
} Buf;

typedef struct {
    double w_mm, h_mm;
    Buf    content;
} Page;

typedef struct {
    int      w, h;
    uint8_t *data;       /* RunLengthDecode stream */
    size_t   len;
} Image;

struct PgPdf {
    Page  *pages;
    int    n_pages, cap_pages;
    Image *images;
    int    n_images, cap_images;
    bool   oom;
};

static void buf_printf(Buf *b, const char *fmt, ...)
{
    if (b->oom)
        return;
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        size_t room = b->cap - b->len;
        int n = vsnprintf(b->data ? b->data + b->len : NULL, room, fmt, ap);
        va_end(ap);
        if (n < 0)
            return;
        if ((size_t)n < room) {
            b->len += (size_t)n;
            return;
        }
        size_t ncap = b->cap ? b->cap * 2 : 4096;
        while (ncap - b->len <= (size_t)n)
            ncap *= 2;
        char *nd = realloc(b->data, ncap);
        if (!nd) {
            b->oom = true;
            return;
        }
        b->data = nd;
        b->cap = ncap;
    }
}

static Buf *cur(PgPdf *p)
{
    static Buf sink;                 /* drawing before the first page */
    if (p->n_pages == 0) {
        sink.len = 0;
        return &sink;
    }
    return &p->pages[p->n_pages - 1].content;
}

/* Numbers trimmed of trailing zeros: the content streams stay readable, and
 * a hundred-page template set stays small. */
static const char *num(char *s, double v)
{
    if (fabs(v) < 5e-5)
        v = 0.0;
    snprintf(s, 24, "%.4f", v);
    char *e = s + strlen(s) - 1;
    while (e > s && *e == '0')
        *e-- = '\0';
    if (*e == '.')
        *e = '\0';
    return s;
}

PgPdf *pg_pdf_new(void)
{
    return calloc(1, sizeof(PgPdf));
}

void pg_pdf_free(PgPdf *p)
{
    if (!p)
        return;
    for (int i = 0; i < p->n_pages; i++)
        free(p->pages[i].content.data);
    for (int i = 0; i < p->n_images; i++)
        free(p->images[i].data);
    free(p->pages);
    free(p->images);
    free(p);
}

void pg_pdf_page(PgPdf *p, double w_mm, double h_mm)
{
    if (p->n_pages == p->cap_pages) {
        int ncap = p->cap_pages ? p->cap_pages * 2 : 8;
        Page *np = realloc(p->pages, (size_t)ncap * sizeof *np);
        if (!np) {
            p->oom = true;
            return;
        }
        p->pages = np;
        p->cap_pages = ncap;
    }
    Page *pg = &p->pages[p->n_pages++];
    memset(pg, 0, sizeof *pg);
    pg->w_mm = w_mm;
    pg->h_mm = h_mm;

    /* Everything after this is in millimetres. */
    char a[24];
    buf_printf(&pg->content, "%s 0 0 %s 0 0 cm\n1 J 1 j\n",
               num(a, PT_PER_MM), num(a, PT_PER_MM));
}

void pg_pdf_stroke_rgb(PgPdf *p, int r, int g, int b)
{
    buf_printf(cur(p), "%.3f %.3f %.3f RG\n", r / 255.0, g / 255.0, b / 255.0);
}

void pg_pdf_fill_rgb(PgPdf *p, int r, int g, int b)
{
    buf_printf(cur(p), "%.3f %.3f %.3f rg\n", r / 255.0, g / 255.0, b / 255.0);
}

void pg_pdf_line_width(PgPdf *p, double mm)
{
    char a[24];
    buf_printf(cur(p), "%s w\n", num(a, mm));
}

void pg_pdf_dash(PgPdf *p, double on_mm, double off_mm)
{
    char a[24], b[24];
    if (on_mm <= 0.0)
        buf_printf(cur(p), "[] 0 d\n");
    else
        buf_printf(cur(p), "[%s %s] 0 d\n", num(a, on_mm), num(b, off_mm));
}

void pg_pdf_move(PgPdf *p, double x, double y)
{
    char a[24], b[24];
    buf_printf(cur(p), "%s %s m\n", num(a, x), num(b, y));
}

void pg_pdf_line(PgPdf *p, double x, double y)
{
    char a[24], b[24];
    buf_printf(cur(p), "%s %s l\n", num(a, x), num(b, y));
}

void pg_pdf_curve(PgPdf *p, double x1, double y1, double x2, double y2,
                  double x3, double y3)
{
    char a[24], b[24], c[24], d[24], e[24], f[24];
    buf_printf(cur(p), "%s %s %s %s %s %s c\n", num(a, x1), num(b, y1),
               num(c, x2), num(d, y2), num(e, x3), num(f, y3));
}

void pg_pdf_close(PgPdf *p)       { buf_printf(cur(p), "h\n"); }
void pg_pdf_stroke(PgPdf *p)      { buf_printf(cur(p), "S\n"); }
void pg_pdf_fill(PgPdf *p)        { buf_printf(cur(p), "f\n"); }
void pg_pdf_fill_stroke(PgPdf *p) { buf_printf(cur(p), "B\n"); }

void pg_pdf_rect(PgPdf *p, double x, double y, double w, double h)
{
    char a[24], b[24], c[24], d[24];
    buf_printf(cur(p), "%s %s %s %s re\n", num(a, x), num(b, y),
               num(c, w), num(d, h));
}

void pg_pdf_segment(PgPdf *p, double x0, double y0, double x1, double y1)
{
    pg_pdf_move(p, x0, y0);
    pg_pdf_line(p, x1, y1);
    pg_pdf_stroke(p);
}

static void xf_point(const PgPdfXf *xf, double x, double y, double *ox, double *oy)
{
    pg_xform(x * xf->scale, y * xf->scale, xf->rot, xf->dx, xf->dy, ox, oy);
}

void pg_pdf_outline(PgPdf *p, const PgVert *v, int n, bool closed,
                    const PgPdfXf *xf)
{
    if (n <= 0)
        return;
    double x, y;
    xf_point(xf, v[0].x, v[0].y, &x, &y);
    pg_pdf_move(p, x, y);

    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; i++) {
        const PgVert *a = &v[i], *b = &v[(i + 1) % n];
        if (fabs(a->bulge) < 1e-12) {
            xf_point(xf, b->x, b->y, &x, &y);
            pg_pdf_line(p, x, y);
            continue;
        }
        /* Arc as cubic Béziers of at most a quarter turn each; handle
         * length k = 4/3 tan(theta/4) of the radius. The control points are
         * built in the part's own frame and transformed, which is exact for
         * a similarity transform. */
        double cx, cy, r, a0, sw;
        pg_bulge_arc(a->x, a->y, b->x, b->y, a->bulge, &cx, &cy, &r, &a0, &sw);
        int k = (int)ceil(fabs(sw) / (M_PI / 2.0));
        if (k < 1) k = 1;
        double step = sw / k;
        double h = 4.0 / 3.0 * tan(step / 4.0);
        for (int j = 0; j < k; j++) {
            double t0 = a0 + step * j, t1 = t0 + step;
            double p0x = cx + r * cos(t0), p0y = cy + r * sin(t0);
            double p3x = cx + r * cos(t1), p3y = cy + r * sin(t1);
            double c1x = p0x - h * r * sin(t0), c1y = p0y + h * r * cos(t0);
            double c2x = p3x + h * r * sin(t1), c2y = p3y - h * r * cos(t1);
            if (j == k - 1) { p3x = b->x; p3y = b->y; }   /* land exactly */
            double X1, Y1, X2, Y2, X3, Y3;
            xf_point(xf, c1x, c1y, &X1, &Y1);
            xf_point(xf, c2x, c2y, &X2, &Y2);
            xf_point(xf, p3x, p3y, &X3, &Y3);
            pg_pdf_curve(p, X1, Y1, X2, Y2, X3, Y3);
        }
    }
    if (closed)
        pg_pdf_close(p);
}

/* ---- images ---------------------------------------------------------- */

/* PDF RunLengthDecode: a length byte L then L+1 literal bytes (L < 128), or
 * 257-L copies of one byte (L > 128); 128 ends the data. A rendered view is
 * mostly flat background, which this compresses well. */
static size_t rle_encode(const uint8_t *in, size_t n, uint8_t *out)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && in[i + run] == in[i])
            run++;
        if (run >= 2) {
            out[o++] = (uint8_t)(257 - run);
            out[o++] = in[i];
            i += run;
        } else {
            size_t lit = 1;
            while (i + lit < n && lit < 128 &&
                   !(i + lit + 1 < n && in[i + lit] == in[i + lit + 1]))
                lit++;
            out[o++] = (uint8_t)(lit - 1);
            memcpy(out + o, in + i, lit);
            o += lit;
            i += lit;
        }
    }
    out[o++] = 128;
    return o;
}

void pg_pdf_image(PgPdf *p, double x, double y, double w, double h,
                  const unsigned char *rgba, int iw, int ih)
{
    if (iw <= 0 || ih <= 0 || p->n_pages == 0)
        return;
    if (p->n_images == p->cap_images) {
        int ncap = p->cap_images ? p->cap_images * 2 : 4;
        Image *ni = realloc(p->images, (size_t)ncap * sizeof *ni);
        if (!ni) {
            p->oom = true;
            return;
        }
        p->images = ni;
        p->cap_images = ncap;
    }

    size_t n = (size_t)iw * (size_t)ih * 3;
    uint8_t *rgb = malloc(n);
    uint8_t *enc = malloc(n + n / 128 + 16);
    if (!rgb || !enc) {
        free(rgb);
        free(enc);
        p->oom = true;
        return;
    }
    for (size_t i = 0, j = 0; i < n; i += 3, j += 4) {
        rgb[i] = rgba[j];
        rgb[i + 1] = rgba[j + 1];
        rgb[i + 2] = rgba[j + 2];
    }
    Image *im = &p->images[p->n_images];
    im->w = iw;
    im->h = ih;
    im->len = rle_encode(rgb, n, enc);
    im->data = enc;
    free(rgb);

    char a[24], b[24], c[24], d[24];
    buf_printf(cur(p), "q %s 0 0 %s %s %s cm /Im%d Do Q\n", num(a, w), num(b, h),
               num(c, x), num(d, y), p->n_images);
    p->n_images++;
}

/* ---- text ------------------------------------------------------------ */

/* Advance widths, 1/1000 em, for ASCII 32..126 — from Adobe's Helvetica and
 * Helvetica-Bold AFM files. Needed only to centre and right-align. */
static const short HELV[95] = {
    278,278,355,556,556,889,667,191,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,278,278,584,584,584,556,
    1015,667,667,722,722,667,611,778,722,278,500,667,556,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,278,278,278,469,556,
    333,556,556,500,556,556,278,556,556,222,222,500,222,833,556,556,
    556,556,333,500,278,556,500,722,500,500,500,334,260,334,584
};
static const short HELV_B[95] = {
    278,333,474,556,556,889,722,238,333,333,389,584,278,333,278,278,
    556,556,556,556,556,556,556,556,556,556,333,333,584,584,584,611,
    975,722,722,722,722,667,611,778,722,278,556,722,611,833,722,778,
    667,778,722,667,611,722,667,944,667,667,611,333,278,333,584,556,
    333,556,611,556,611,556,333,611,611,278,278,556,278,889,611,611,
    611,611,389,556,333,611,556,778,556,556,500,389,280,389,584
};

/* UTF-8 to WinAnsiEncoding, for the handful of non-ASCII characters a
 * drawing uses. Returns the byte count written (at most cap-1). */
static size_t to_winansi(const char *s, unsigned char *out, size_t cap)
{
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n + 1 < cap) {
        unsigned cp;
        if (p[0] < 0x80) {
            cp = *p++;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
            cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
            p += 3;
        } else {
            p++;
            cp = '?';
            while ((*p & 0xC0) == 0x80) p++;
        }

        unsigned char c;
        if (cp < 0x80)                    c = (unsigned char)cp;
        else if (cp >= 0xA0 && cp <= 0xFF) c = (unsigned char)cp;
        else if (cp == 0x2014)            c = 0x97;   /* em dash  */
        else if (cp == 0x2013)            c = 0x96;   /* en dash  */
        else if (cp == 0x2022)            c = 0x95;   /* bullet   */
        else if (cp == 0x2026)            c = 0x85;   /* ellipsis */
        else if (cp == 0x2018)            c = 0x91;
        else if (cp == 0x2019)            c = 0x92;
        else if (cp == 0x201C)            c = 0x93;
        else if (cp == 0x201D)            c = 0x94;
        else if (cp == 0x2212)            c = '-';
        else                              c = '?';
        out[n++] = c;
    }
    out[n] = '\0';
    return n;
}

static int glyph_w(unsigned char c, PgPdfFont font)
{
    if (c >= 32 && c <= 126)
        return (font == PG_PDF_BOLD ? HELV_B : HELV)[c - 32];
    switch (c) {
    case 0xB0: return 400;    /* degree        */
    case 0xB1: return 584;    /* plus-minus    */
    case 0xD7: return 584;    /* multiply      */
    case 0xB7: return 278;    /* middle dot    */
    case 0x97: return 1000;   /* em dash       */
    case 0x96: return 556;    /* en dash       */
    case 0x95: return 350;    /* bullet        */
    case 0xD8: return 778;    /* O slash       */
    default:   return 556;
    }
}

double pg_pdf_text_width(double size_pt, PgPdfFont font, const char *utf8)
{
    unsigned char w[512];
    size_t n = to_winansi(utf8, w, sizeof w);
    long units = 0;
    for (size_t i = 0; i < n; i++)
        units += glyph_w(w[i], font);
    return units / 1000.0 * size_pt / PT_PER_MM;
}

void pg_pdf_text(PgPdf *p, double x, double y, double size_pt, PgPdfFont font,
                 PgPdfAlign align, double rot_deg, const char *utf8)
{
    unsigned char w[512];
    size_t n = to_winansi(utf8, w, sizeof w);
    if (n == 0)
        return;

    double width = pg_pdf_text_width(size_pt, font, utf8);
    double shift = align == PG_PDF_CENTRE ? -width / 2.0
                 : align == PG_PDF_RIGHT  ? -width : 0.0;
    double r = rot_deg * M_PI / 180.0;
    double c = cos(r), s = sin(r);
    double tx = x + shift * c, ty = y + shift * s;

    Buf *b = cur(p);
    char A[24], B[24], C[24], D[24], E[24], F[24], S[24];
    buf_printf(b, "BT /F%d %s Tf %s %s %s %s %s %s Tm (",
               font == PG_PDF_BOLD ? 2 : 1, num(S, size_pt / PT_PER_MM),
               num(A, c), num(B, s), num(C, -s), num(D, c),
               num(E, tx), num(F, ty));
    for (size_t i = 0; i < n; i++) {
        unsigned char ch = w[i];
        if (ch == '(' || ch == ')' || ch == '\\')
            buf_printf(b, "\\%c", ch);
        else if (ch < 32 || ch > 126)
            buf_printf(b, "\\%03o", ch);
        else
            buf_printf(b, "%c", ch);
    }
    buf_printf(b, ") Tj ET\n");
}

/* ---- file ------------------------------------------------------------ */

static void pdf_string(FILE *f, const char *utf8)
{
    unsigned char w[256];
    size_t n = to_winansi(utf8, w, sizeof w);
    fputc('(', f);
    for (size_t i = 0; i < n; i++) {
        if (w[i] == '(' || w[i] == ')' || w[i] == '\\')
            fprintf(f, "\\%c", w[i]);
        else if (w[i] < 32 || w[i] > 126)
            fprintf(f, "\\%03o", w[i]);
        else
            fputc(w[i], f);
    }
    fputc(')', f);
}

bool pg_pdf_save(PgPdf *p, const char *path, const char *title,
                 char *err, size_t errcap)
{
    if (p->oom) {
        snprintf(err, errcap, "Out of memory while building the PDF.");
        return false;
    }
    for (int i = 0; i < p->n_pages; i++) {
        if (p->pages[i].content.oom) {
            snprintf(err, errcap, "Out of memory while building the PDF.");
            return false;
        }
    }
    if (p->n_pages == 0) {
        snprintf(err, errcap, "The PDF has no pages.");
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        plat_write_error(err, errcap, path, errno);
        return false;
    }

    /* objects: 1 catalog, 2 page tree, 3-4 fonts, 5 info, then a page and
     * its content stream per page */
    int n_obj = 5 + 2 * p->n_pages + p->n_images;
    int first_image = 6 + 2 * p->n_pages;
    long *off = calloc((size_t)n_obj + 1, sizeof *off);
    if (!off) {
        fclose(f);
        snprintf(err, errcap, "Out of memory while writing the PDF.");
        return false;
    }

    fprintf(f, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");

    off[1] = ftell(f);
    fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    off[2] = ftell(f);
    fprintf(f, "2 0 obj\n<< /Type /Pages /Count %d /Kids [", p->n_pages);
    for (int i = 0; i < p->n_pages; i++)
        fprintf(f, " %d 0 R", 6 + 2 * i);
    fprintf(f, " ] >>\nendobj\n");

    off[3] = ftell(f);
    fprintf(f, "3 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica "
               "/Encoding /WinAnsiEncoding >>\nendobj\n");
    off[4] = ftell(f);
    fprintf(f, "4 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont "
               "/Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n");

    PlatDate dt;
    plat_date_now(&dt);
    off[5] = ftell(f);
    fprintf(f, "5 0 obj\n<< /Title ");
    pdf_string(f, title ? title : "");
    fprintf(f, " /Producer (" PIPEGEN_NAME " " PIPEGEN_VERSION ")"
               " /CreationDate (D:%04d%02d%02d%02d%02d%02d) >>\nendobj\n",
            dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

    for (int i = 0; i < p->n_pages; i++) {
        Page *pg = &p->pages[i];
        int po = 6 + 2 * i, co = 7 + 2 * i;

        off[po] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent 2 0 R "
                   "/MediaBox [0 0 %.3f %.3f] "
                   "/Resources << /Font << /F1 3 0 R /F2 4 0 R >>",
                po, pg->w_mm * PT_PER_MM, pg->h_mm * PT_PER_MM);
        /* every page may use every image; unused entries are harmless */
        if (p->n_images) {
            fprintf(f, " /XObject <<");
            for (int k = 0; k < p->n_images; k++)
                fprintf(f, " /Im%d %d 0 R", k, first_image + k);
            fprintf(f, " >>");
        }
        fprintf(f, " >> /Contents %d 0 R >>\nendobj\n", co);

        off[co] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %zu >>\nstream\n", co, pg->content.len);
        if (pg->content.len)
            fwrite(pg->content.data, 1, pg->content.len, f);
        fprintf(f, "\nendstream\nendobj\n");
    }

    for (int k = 0; k < p->n_images; k++) {
        const Image *im = &p->images[k];
        off[first_image + k] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %d "
                   "/Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 "
                   "/Filter /RunLengthDecode /Length %zu >>\nstream\n",
                first_image + k, im->w, im->h, im->len);
        fwrite(im->data, 1, im->len, f);
        fprintf(f, "\nendstream\nendobj\n");
    }

    long xref = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", n_obj + 1);
    for (int i = 1; i <= n_obj; i++)
        fprintf(f, "%010ld 00000 n \n", off[i]);
    fprintf(f, "trailer\n<< /Size %d /Root 1 0 R /Info 5 0 R >>\n"
               "startxref\n%ld\n%%%%EOF\n", n_obj + 1, xref);
    free(off);

    bool ok = !ferror(f);
    ok = (fclose(f) == 0) && ok;
    if (!ok)
        snprintf(err, errcap, "Writing %s failed.", path);
    return ok;
}
