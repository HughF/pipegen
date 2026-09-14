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
#include "pg_glview.h"
#include "pg_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The projection is the software rasteriser's, written as a clip-space
 * transform: view space is (right, up, forward) from pg_camera_frame, the
 * screen is focal / depth, and depth runs to infinity from the near plane
 * (z_ndc = 1 - 2 near / z), which a 24-bit buffer resolves to a fraction of
 * a millimetre at any sensible distance.
 *
 * The texture's first row is the top of the view, so the projection flips y
 * and a pixel read at (x, y) is counted from the top, as the interface does.
 *
 * A bias moves a vertex along its ray towards the eye by that fraction of its
 * depth — the same thing the software rasteriser's bias does to 1/z — so a
 * line drawn on a surface wins against it without moving on the screen.
 */

#define ID_COLOUR_GLSL                                                     \
    "vec4 id_colour(int id) {\n"                                           \
    "    int k = id + 1;\n"                                                \
    "    return vec4(float(k & 255), float((k >> 8) & 255),\n"             \
    "                float((k >> 16) & 255), 255.0) / 255.0;\n"            \
    "}\n"

static const char *TRI_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3  a_pos;\n"
    "layout(location = 1) in vec3  a_nrm;\n"
    "layout(location = 2) in vec4  a_col;\n"
    "layout(location = 3) in int   a_id;\n"
    "layout(location = 4) in float a_bias;\n"
    "uniform vec3  u_eye, u_r, u_u, u_f;\n"
    "uniform vec2  u_proj;\n"
    "uniform float u_near;\n"
    "out vec3 v_pos;\n"
    "out vec3 v_nrm;\n"
    "out vec4 v_col;\n"
    "flat out int v_id;\n"
    "flat out int v_lit;\n"
    "void main() {\n"
    "    vec3 d = a_pos - u_eye;\n"
    "    vec3 v = vec3(dot(d, u_r), dot(d, u_u), dot(d, u_f)) / (1.0 + a_bias);\n"
    "    gl_Position = vec4(v.x * u_proj.x, v.y * u_proj.y, v.z - 2.0 * u_near, v.z);\n"
    "    v_pos = a_pos;\n"
    "    v_nrm = a_nrm;\n"
    "    v_col = a_col;\n"
    "    v_id  = a_id;\n"
    "    v_lit = dot(a_nrm, a_nrm) > 0.25 ? 1 : 0;\n"
    "}\n";

/* The shading is pg_view3d's, per pixel instead of per vertex. */
static const char *TRI_FS =
    "#version 330 core\n"
    "in vec3 v_pos;\n"
    "in vec3 v_nrm;\n"
    "in vec4 v_col;\n"
    "flat in int v_id;\n"
    "flat in int v_lit;\n"
    "uniform vec3 u_eye, u_light;\n"
    "uniform int  u_pick;\n"
    "out vec4 o_col;\n"
    ID_COLOUR_GLSL
    "void main() {\n"
    "    if (u_pick != 0) { o_col = id_colour(v_id); return; }\n"
    "    vec3 c = v_col.rgb;\n"
    "    if (v_lit != 0) {\n"
    "        vec3 n = normalize(v_nrm);\n"
    "        vec3 vd = normalize(u_eye - v_pos);\n"
    "        float inside = 1.0;\n"
    "        if (dot(n, vd) < 0.0) { n = -n; inside = 0.55; }\n"
    "        float diff = max(0.0, dot(n, u_light));\n"
    "        float fill = max(0.0, dot(n, vd));\n"
    "        vec3 h = normalize(u_light + vd);\n"
    "        float spec = pow(max(0.0, dot(n, h)), 40.0) * (90.0 / 255.0) * inside;\n"
    "        float k = (0.26 + 0.58 * diff + 0.24 * fill) * inside;\n"
    "        c = min(vec3(1.0), c * k + spec);\n"
    "    }\n"
    "    o_col = vec4(c, v_col.a);\n"
    "}\n";

/*
 * A line is a quad expanded in screen space, so it keeps its width in pixels
 * from any distance — core OpenGL has no wide lines. The segment is clipped
 * to the near plane in view space first: projecting a point behind the eye
 * would turn the quad inside out.
 */
static const char *LINE_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3  a_a;\n"
    "layout(location = 1) in vec3  a_b;\n"
    "layout(location = 2) in vec2  a_se;\n"         /* side -1/+1, end 0/1 */
    "layout(location = 3) in vec4  a_col;\n"
    "layout(location = 4) in int   a_id;\n"
    "layout(location = 5) in float a_width;\n"
    "layout(location = 6) in float a_bias;\n"
    "uniform vec3  u_eye, u_r, u_u, u_f;\n"
    "uniform vec2  u_half;\n"                       /* w/2, h/2 */
    "uniform float u_focal, u_near, u_width, u_flip;\n"
    "out vec4 v_col;\n"
    "flat out int v_id;\n"
    "vec3 view(vec3 p) {\n"
    "    vec3 d = p - u_eye;\n"
    "    return vec3(dot(d, u_r), dot(d, u_u), dot(d, u_f));\n"
    "}\n"
    "void main() {\n"
    "    v_col = a_col;\n"
    "    v_id = a_id;\n"
    "    vec3 va = view(a_a), vb = view(a_b);\n"
    "    if (va.z < u_near && vb.z < u_near) {\n"
    "        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    if (va.z < u_near) va = mix(va, vb, (u_near - va.z) / (vb.z - va.z));\n"
    "    if (vb.z < u_near) vb = mix(vb, va, (u_near - vb.z) / (va.z - vb.z));\n"
    "    vec2 sa = va.xy * (u_focal / va.z), sb = vb.xy * (u_focal / vb.z);\n"
    "    vec2 dir = sb - sa;\n"
    "    float len = length(dir);\n"
    "    dir = len < 1e-4 ? vec2(1.0, 0.0) : dir / len;\n"
    "    vec2 nrm = vec2(-dir.y, dir.x);\n"
    "    float w = max(1.0, a_width * u_width);\n"
    "    bool at_b = a_se.y > 0.5;\n"
    "    vec2 s = (at_b ? sb : sa) + nrm * (a_se.x * w * 0.5)\n"
    "           + dir * ((at_b ? 1.0 : -1.0) * w * 0.35);\n"
    "    float z = (at_b ? vb.z : va.z) / (1.0 + a_bias);\n"
    "    gl_Position = vec4(s.x / u_half.x * z, u_flip * s.y / u_half.y * z,\n"
    "                       z - 2.0 * u_near, z);\n"
    "}\n";

static const char *LINE_FS =
    "#version 330 core\n"
    "in vec4 v_col;\n"
    "flat in int v_id;\n"
    "uniform int u_pick;\n"
    "out vec4 o_col;\n"
    ID_COLOUR_GLSL
    "void main() {\n"
    "    o_col = u_pick != 0 ? id_colour(v_id) : v_col;\n"
    "}\n";

/* One triangle over the whole view, no vertex data. */
static const char *BG_VS =
    "#version 330 core\n"
    "void main() {\n"
    "    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "}\n";

static const char *BG_FS =
    "#version 330 core\n"
    "uniform vec3  u_top, u_bottom;\n"
    "uniform float u_h;\n"
    "out vec4 o_col;\n"
    "void main() {\n"
    "    float t = clamp((gl_FragCoord.y - 0.5) / max(1.0, u_h - 1.0), 0.0, 1.0);\n"
    "    o_col = vec4(mix(u_top, u_bottom, t), 1.0);\n"
    "}\n";

typedef struct {
    GLuint prog;
    GLint  eye, r, u, f, near, pick;
    GLint  proj, light;                    /* triangles */
    GLint  half, focal, width, flip;       /* lines     */
} Prog;

typedef struct {
    float   a[3], b[3];
    float   se[2];
    uint8_t col[4];
    int32_t id;
    float   width, bias;
} LineVert;

struct PgGlView {
    Prog   tri, line;
    GLuint bg;
    GLint  bg_top, bg_bottom, bg_h;

    GLuint vao_tri, vbo_tri, ebo_tri, ebo_glass;
    GLuint vao_line, vbo_line, ebo_line;
    GLuint vao_empty;
    int    n_tri, n_glass, n_line_idx;

    int    w, h, samples, max_samples;
    GLuint fbo_ms, rb_ms_col, rb_ms_depth;
    GLuint fbo_out, tex_out, rb_out_depth;
    GLuint fbo_pick, tex_pick, rb_pick_depth;

    /* the last render, for the pick pass */
    bool          rendered, pick_valid;
    int           pick_x, pick_y, pick_id;
    PgViewFrame   fr;
    float         line_scale;
};

static void locate(Prog *p)
{
    p->eye   = gl.GetUniformLocation(p->prog, "u_eye");
    p->r     = gl.GetUniformLocation(p->prog, "u_r");
    p->u     = gl.GetUniformLocation(p->prog, "u_u");
    p->f     = gl.GetUniformLocation(p->prog, "u_f");
    p->near  = gl.GetUniformLocation(p->prog, "u_near");
    p->pick  = gl.GetUniformLocation(p->prog, "u_pick");
    p->proj  = gl.GetUniformLocation(p->prog, "u_proj");
    p->light = gl.GetUniformLocation(p->prog, "u_light");
    p->half  = gl.GetUniformLocation(p->prog, "u_half");
    p->focal = gl.GetUniformLocation(p->prog, "u_focal");
    p->width = gl.GetUniformLocation(p->prog, "u_width");
    p->flip  = gl.GetUniformLocation(p->prog, "u_flip");
}

PgGlView *pg_glview_new(char *err, size_t cap)
{
    PgGlView *v = calloc(1, sizeof *v);
    if (!v) {
        snprintf(err, cap, "out of memory");
        return NULL;
    }
    v->tri.prog = pg_gl_program(TRI_VS, TRI_FS, err, cap);
    v->line.prog = v->tri.prog ? pg_gl_program(LINE_VS, LINE_FS, err, cap) : 0;
    v->bg = v->line.prog ? pg_gl_program(BG_VS, BG_FS, err, cap) : 0;
    if (!v->bg) {
        pg_glview_free(v);
        return NULL;
    }
    locate(&v->tri);
    locate(&v->line);
    v->bg_top = gl.GetUniformLocation(v->bg, "u_top");
    v->bg_bottom = gl.GetUniformLocation(v->bg, "u_bottom");
    v->bg_h = gl.GetUniformLocation(v->bg, "u_h");

    gl.GenVertexArrays(1, &v->vao_tri);
    gl.GenVertexArrays(1, &v->vao_line);
    gl.GenVertexArrays(1, &v->vao_empty);
    gl.GenBuffers(1, &v->vbo_tri);
    gl.GenBuffers(1, &v->ebo_tri);
    gl.GenBuffers(1, &v->ebo_glass);
    gl.GenBuffers(1, &v->vbo_line);
    gl.GenBuffers(1, &v->ebo_line);

    gl.BindVertexArray(v->vao_tri);
    gl.BindBuffer(GL_ARRAY_BUFFER, v->vbo_tri);
    GLsizei vs = sizeof(PgMeshVert);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, vs, (void *)offsetof(PgMeshVert, p));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, vs, (void *)offsetof(PgMeshVert, n));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, vs, (void *)offsetof(PgMeshVert, col));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribIPointer(3, 1, GL_INT, vs, (void *)offsetof(PgMeshVert, id));
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, vs, (void *)offsetof(PgMeshVert, bias));

    gl.BindVertexArray(v->vao_line);
    gl.BindBuffer(GL_ARRAY_BUFFER, v->vbo_line);
    GLsizei ls = sizeof(LineVert);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ls, (void *)offsetof(LineVert, a));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, ls, (void *)offsetof(LineVert, b));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, ls, (void *)offsetof(LineVert, se));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, ls, (void *)offsetof(LineVert, col));
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribIPointer(4, 1, GL_INT, ls, (void *)offsetof(LineVert, id));
    gl.EnableVertexAttribArray(5);
    gl.VertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, ls, (void *)offsetof(LineVert, width));
    gl.EnableVertexAttribArray(6);
    gl.VertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, ls, (void *)offsetof(LineVert, bias));
    gl.BindVertexArray(0);

    GLint ms = 0;
    gl.GetIntegerv(GL_MAX_SAMPLES, &ms);
    v->max_samples = ms < 4 ? ms : 4;
    if (gl.GetError() != GL_NO_ERROR) {
        snprintf(err, cap, "OpenGL error setting up the 3D view");
        pg_glview_free(v);
        return NULL;
    }
    return v;
}

static void free_targets(PgGlView *v)
{
    gl.DeleteFramebuffers(1, &v->fbo_ms);
    gl.DeleteFramebuffers(1, &v->fbo_out);
    gl.DeleteFramebuffers(1, &v->fbo_pick);
    gl.DeleteRenderbuffers(1, &v->rb_ms_col);
    gl.DeleteRenderbuffers(1, &v->rb_ms_depth);
    gl.DeleteRenderbuffers(1, &v->rb_out_depth);
    gl.DeleteRenderbuffers(1, &v->rb_pick_depth);
    gl.DeleteTextures(1, &v->tex_out);
    gl.DeleteTextures(1, &v->tex_pick);
    v->fbo_ms = v->fbo_out = v->fbo_pick = 0;
    v->rb_ms_col = v->rb_ms_depth = v->rb_out_depth = v->rb_pick_depth = 0;
    v->tex_out = v->tex_pick = 0;
    v->w = v->h = 0;
}

void pg_glview_free(PgGlView *v)
{
    if (!v)
        return;
    if (gl.DeleteProgram) {
        free_targets(v);
        gl.DeleteProgram(v->tri.prog);
        gl.DeleteProgram(v->line.prog);
        gl.DeleteProgram(v->bg);
        gl.DeleteVertexArrays(1, &v->vao_tri);
        gl.DeleteVertexArrays(1, &v->vao_line);
        gl.DeleteVertexArrays(1, &v->vao_empty);
        gl.DeleteBuffers(1, &v->vbo_tri);
        gl.DeleteBuffers(1, &v->ebo_tri);
        gl.DeleteBuffers(1, &v->ebo_glass);
        gl.DeleteBuffers(1, &v->vbo_line);
        gl.DeleteBuffers(1, &v->ebo_line);
    }
    free(v);
}

bool pg_glview_upload(PgGlView *v, const PgMesh *m)
{
    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, v->vbo_tri);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m->n_vert * sizeof(PgMeshVert),
                  m->vert, GL_STATIC_DRAW);
    gl.BindVertexArray(v->vao_tri);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_glass);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m->n_glass * sizeof(uint32_t),
                  m->glass, GL_STATIC_DRAW);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_tri);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)m->n_tri * sizeof(uint32_t),
                  m->tri, GL_STATIC_DRAW);
    v->n_tri = m->n_tri;
    v->n_glass = m->n_glass;

    /* four corners and two triangles a line */
    size_t nl = (size_t)m->n_line;
    LineVert *lv = malloc(nl * 4 * sizeof *lv + 1);
    uint32_t *li = malloc(nl * 6 * sizeof *li + 1);
    bool ok = lv && li;
    if (ok) {
        static const float se[4][2] = { { 1, 0 }, { -1, 0 }, { -1, 1 }, { 1, 1 } };
        for (size_t i = 0; i < nl; i++) {
            const PgMeshLine *l = &m->line[i];
            for (int k = 0; k < 4; k++) {
                LineVert *q = &lv[i * 4 + k];
                memcpy(q->a, l->a, sizeof q->a);
                memcpy(q->b, l->b, sizeof q->b);
                memcpy(q->se, se[k], sizeof q->se);
                memcpy(q->col, l->col, sizeof q->col);
                q->id = l->id;
                q->width = l->width;
                q->bias = l->bias;
            }
            uint32_t b = (uint32_t)(i * 4);
            const uint32_t quad[6] = { b, b + 1, b + 2, b, b + 2, b + 3 };
            memcpy(li + i * 6, quad, sizeof quad);
        }
    }
    gl.BindVertexArray(0);
    gl.BindBuffer(GL_ARRAY_BUFFER, v->vbo_line);
    gl.BufferData(GL_ARRAY_BUFFER, ok ? (GLsizeiptr)(nl * 4 * sizeof *lv) : 0, lv,
                  GL_STATIC_DRAW);
    gl.BindVertexArray(v->vao_line);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_line);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, ok ? (GLsizeiptr)(nl * 6 * sizeof *li) : 0,
                  li, GL_STATIC_DRAW);
    gl.BindVertexArray(0);
    v->n_line_idx = ok ? (int)(nl * 6) : 0;
    free(lv);
    free(li);
    v->pick_valid = false;
    return ok;
}

static GLuint colour_texture(int w, int h)
{
    GLuint t = 0;
    gl.GenTextures(1, &t);
    gl.BindTexture(GL_TEXTURE_2D, t);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    return t;
}

static GLuint depth_buffer(int w, int h, int samples)
{
    GLuint rb = 0;
    gl.GenRenderbuffers(1, &rb);
    gl.BindRenderbuffer(GL_RENDERBUFFER, rb);
    if (samples > 0)
        gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
    else
        gl.RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    return rb;
}

static bool texture_target(GLuint *fbo, GLuint *tex, GLuint *depth, int w, int h)
{
    *tex = colour_texture(w, h);
    *depth = depth_buffer(w, h, 0);
    gl.GenFramebuffers(1, fbo);
    gl.BindFramebuffer(GL_FRAMEBUFFER, *fbo);
    gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
    gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth);
    return gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

static bool ensure_targets(PgGlView *v, int w, int h)
{
    if (v->fbo_out && v->w == w && v->h == h)
        return true;
    free_targets(v);

    bool ok = texture_target(&v->fbo_out, &v->tex_out, &v->rb_out_depth, w, h) &&
              texture_target(&v->fbo_pick, &v->tex_pick, &v->rb_pick_depth, w, h);

    /* Multisampled drawing, resolved into tex_out. A driver that cannot
     * make it gets aliased edges rather than no view. */
    v->samples = 0;
    if (ok && v->max_samples > 1) {
        gl.GenRenderbuffers(1, &v->rb_ms_col);
        gl.BindRenderbuffer(GL_RENDERBUFFER, v->rb_ms_col);
        gl.RenderbufferStorageMultisample(GL_RENDERBUFFER, v->max_samples, GL_RGBA8, w, h);
        v->rb_ms_depth = depth_buffer(w, h, v->max_samples);
        gl.GenFramebuffers(1, &v->fbo_ms);
        gl.BindFramebuffer(GL_FRAMEBUFFER, v->fbo_ms);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, v->rb_ms_col);
        gl.FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, v->rb_ms_depth);
        if (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            v->samples = v->max_samples;
        } else {
            gl.DeleteFramebuffers(1, &v->fbo_ms);
            gl.DeleteRenderbuffers(1, &v->rb_ms_col);
            gl.DeleteRenderbuffers(1, &v->rb_ms_depth);
            v->fbo_ms = v->rb_ms_col = v->rb_ms_depth = 0;
        }
    }
    gl.BindRenderbuffer(GL_RENDERBUFFER, 0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    while (gl.GetError() != GL_NO_ERROR)
        ;
    if (!ok) {
        free_targets(v);
        return false;
    }
    v->w = w;
    v->h = h;
    return true;
}

static void set_frame(const PgGlView *v, const Prog *p, bool pick)
{
    const PgViewFrame *fr = &v->fr;
    gl.UseProgram(p->prog);
    gl.Uniform3f(p->eye, (float)fr->eye[0], (float)fr->eye[1], (float)fr->eye[2]);
    gl.Uniform3f(p->r, (float)fr->r[0], (float)fr->r[1], (float)fr->r[2]);
    gl.Uniform3f(p->u, (float)fr->u[0], (float)fr->u[1], (float)fr->u[2]);
    gl.Uniform3f(p->f, (float)fr->f[0], (float)fr->f[1], (float)fr->f[2]);
    gl.Uniform1f(p->near, (float)fr->near_z);
    gl.Uniform1i(p->pick, pick ? 1 : 0);
    if (p->proj >= 0)
        gl.Uniform2f(p->proj, (float)(fr->focal / (v->w / 2.0)),
                     (float)(-fr->focal / (v->h / 2.0)));
    if (p->light >= 0)
        gl.Uniform3f(p->light, (float)fr->light[0], (float)fr->light[1], (float)fr->light[2]);
    if (p->half >= 0)
        gl.Uniform2f(p->half, (float)(v->w / 2.0), (float)(v->h / 2.0));
    if (p->focal >= 0)
        gl.Uniform1f(p->focal, (float)fr->focal);
    if (p->flip >= 0)
        gl.Uniform1f(p->flip, -1.0f);
    /* Joint rings are thin to look at and fiddly to point at: pick them a
     * little wider than they are drawn. */
    if (p->width >= 0)
        gl.Uniform1f(p->width, v->line_scale * (pick ? 1.6f : 1.0f));
}

static void draw_scene(const PgGlView *v, bool pick)
{
    gl.Enable(GL_DEPTH_TEST);
    gl.DepthFunc(GL_LESS);
    gl.DepthMask(GL_TRUE);
    gl.Disable(GL_BLEND);

    if (v->n_tri) {
        set_frame(v, &v->tri, pick);
        gl.BindVertexArray(v->vao_tri);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_tri);
        gl.DrawElements(GL_TRIANGLES, v->n_tri, GL_UNSIGNED_INT, NULL);
    }
    if (v->n_line_idx) {
        set_frame(v, &v->line, pick);
        gl.BindVertexArray(v->vao_line);
        gl.DrawElements(GL_TRIANGLES, v->n_line_idx, GL_UNSIGNED_INT, NULL);
    }
    /* translucent surfaces: blended, depth-tested, never take a pixel */
    if (!pick && v->n_glass) {
        gl.Enable(GL_BLEND);
        gl.BlendEquation(GL_FUNC_ADD);
        gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        gl.DepthMask(GL_FALSE);
        set_frame(v, &v->tri, false);
        gl.BindVertexArray(v->vao_tri);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_glass);
        gl.DrawElements(GL_TRIANGLES, v->n_glass, GL_UNSIGNED_INT, NULL);
        gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, v->ebo_tri);
        gl.DepthMask(GL_TRUE);
        gl.Disable(GL_BLEND);
    }
    gl.BindVertexArray(0);
    gl.Disable(GL_DEPTH_TEST);
}

bool pg_glview_render(PgGlView *v, const PgCamera *cam, int w, int h,
                      const PgViewStyle *st, float line_scale)
{
    if (w < 1 || h < 1 || !ensure_targets(v, w, h))
        return false;
    pg_camera_frame(cam, w, h, &v->fr);
    v->line_scale = line_scale;

    gl.BindFramebuffer(GL_FRAMEBUFFER, v->samples ? v->fbo_ms : v->fbo_out);
    gl.Viewport(0, 0, w, h);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_BLEND);
    gl.Disable(GL_DEPTH_TEST);
    gl.DepthMask(GL_TRUE);
    gl.Clear(GL_DEPTH_BUFFER_BIT);

    gl.UseProgram(v->bg);
    gl.Uniform3f(v->bg_top, st->bg_top[0] / 255.0f, st->bg_top[1] / 255.0f,
                 st->bg_top[2] / 255.0f);
    gl.Uniform3f(v->bg_bottom, st->bg_bottom[0] / 255.0f, st->bg_bottom[1] / 255.0f,
                 st->bg_bottom[2] / 255.0f);
    gl.Uniform1f(v->bg_h, (float)h);
    gl.BindVertexArray(v->vao_empty);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);

    draw_scene(v, false);

    if (v->samples) {
        gl.BindFramebuffer(GL_READ_FRAMEBUFFER, v->fbo_ms);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, v->fbo_out);
        gl.BlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.UseProgram(0);
    v->rendered = true;
    v->pick_valid = false;
    return true;
}

unsigned pg_glview_texture(const PgGlView *v)
{
    return v && v->rendered ? v->tex_out : 0;
}

int pg_glview_samples(const PgGlView *v)
{
    return v ? v->samples : 0;
}

int pg_glview_pick(PgGlView *v, int x, int y)
{
    if (!v || !v->rendered || x < 0 || y < 0 || x >= v->w || y >= v->h)
        return -1;
    if (!v->pick_valid) {
        gl.BindFramebuffer(GL_FRAMEBUFFER, v->fbo_pick);
        gl.Viewport(0, 0, v->w, v->h);
        gl.Disable(GL_SCISSOR_TEST);
        gl.DepthMask(GL_TRUE);
        gl.ClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        draw_scene(v, true);
        gl.UseProgram(0);
        v->pick_valid = true;
        v->pick_x = v->pick_y = -1;
    }
    /* A read-back waits for the GPU: not again for the same pixel of the
     * same picture. */
    if (v->pick_x == x && v->pick_y == y)
        return v->pick_id;
    uint8_t px[4] = { 0, 0, 0, 0 };
    gl.BindFramebuffer(GL_FRAMEBUFFER, v->fbo_pick);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    v->pick_x = x;
    v->pick_y = y;
    v->pick_id = (int)(px[0] | (px[1] << 8) | (px[2] << 16)) - 1;
    return v->pick_id;
}
