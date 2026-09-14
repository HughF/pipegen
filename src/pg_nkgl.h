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
 * pg_nkgl.h — Nuklear drawn with OpenGL 3.3
 *
 * Included once, by pg_ui.c, straight after nuklear_sdl_renderer.h's
 * implementation. It shares that backend's context, font atlas and event
 * handling, and replaces only the drawing: when the window has a GL context
 * there is no SDL_Renderer to draw with.
 *
 * Textures in an nk_image are GL texture names (nk_image_id).
 */
#ifndef PG_NKGL_H
#define PG_NKGL_H

#include "pg_gl.h"

static const char *NKGL_VS =
    "#version 330 core\n"
    "uniform mat4 u_proj;\n"
    "layout(location = 0) in vec2 a_pos;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_col;\n"
    "out vec2 v_uv;\n"
    "out vec4 v_col;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    v_col = a_col;\n"
    "    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);\n"
    "}\n";

static const char *NKGL_FS =
    "#version 330 core\n"
    "uniform sampler2D u_tex;\n"
    "in vec2 v_uv;\n"
    "in vec4 v_col;\n"
    "out vec4 o_col;\n"
    "void main() {\n"
    "    o_col = v_col * texture(u_tex, v_uv);\n"
    "}\n";

static struct {
    GLuint prog, vao, vbo, ebo, font;
    GLint  u_proj, u_tex;
} nkgl;

static bool pg_nkgl_init(char *err, size_t cap)
{
    nkgl.prog = pg_gl_program(NKGL_VS, NKGL_FS, err, cap);
    if (!nkgl.prog)
        return false;
    nkgl.u_proj = gl.GetUniformLocation(nkgl.prog, "u_proj");
    nkgl.u_tex = gl.GetUniformLocation(nkgl.prog, "u_tex");

    gl.GenVertexArrays(1, &nkgl.vao);
    gl.GenBuffers(1, &nkgl.vbo);
    gl.GenBuffers(1, &nkgl.ebo);
    gl.BindVertexArray(nkgl.vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, nkgl.vbo);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, nkgl.ebo);
    GLsizei vs = sizeof(struct nk_sdl_vertex);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, vs,
                           (void *)offsetof(struct nk_sdl_vertex, position));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, vs,
                           (void *)offsetof(struct nk_sdl_vertex, uv));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, vs,
                           (void *)offsetof(struct nk_sdl_vertex, col));
    gl.BindVertexArray(0);
    return true;
}

static void pg_nkgl_font_stash_end(void)
{
    int w = 0, h = 0;
    const void *image = nk_font_atlas_bake(&sdl.atlas, &w, &h, NK_FONT_ATLAS_RGBA32);
    gl.GenTextures(1, &nkgl.font);
    gl.BindTexture(GL_TEXTURE_2D, nkgl.font);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    gl.BindTexture(GL_TEXTURE_2D, 0);
    nk_font_atlas_end(&sdl.atlas, nk_handle_id((int)nkgl.font), &sdl.ogl.tex_null);
    if (sdl.atlas.default_font)
        nk_style_set_font(&sdl.ctx, &sdl.atlas.default_font->handle);
}

/* Draw this frame's interface over the whole drawable, dw x dh pixels. */
static void pg_nkgl_render(int dw, int dh, enum nk_anti_aliasing aa)
{
    static const struct nk_draw_vertex_layout_element layout[] = {
        { NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, position) },
        { NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(struct nk_sdl_vertex, uv) },
        { NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(struct nk_sdl_vertex, col) },
        { NK_VERTEX_LAYOUT_END }
    };

    Uint64 now = SDL_GetTicks64();
    sdl.ctx.delta_time_seconds = (float)(now - sdl.time_of_last_frame) / 1000;
    sdl.time_of_last_frame = now;

    struct nk_convert_config config;
    NK_MEMSET(&config, 0, sizeof config);
    config.vertex_layout = layout;
    config.vertex_size = sizeof(struct nk_sdl_vertex);
    config.vertex_alignment = NK_ALIGNOF(struct nk_sdl_vertex);
    config.tex_null = sdl.ogl.tex_null;
    config.circle_segment_count = 22;
    config.curve_segment_count = 22;
    config.arc_segment_count = 22;
    config.global_alpha = 1.0f;
    config.shape_AA = aa;
    config.line_AA = aa;

    struct nk_buffer vbuf, ebuf;
    nk_buffer_init_default(&vbuf);
    nk_buffer_init_default(&ebuf);
    nk_convert(&sdl.ctx, &sdl.ogl.cmds, &vbuf, &ebuf, &config);

    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, dw, dh);
    gl.Enable(GL_BLEND);
    gl.BlendEquation(GL_FUNC_ADD);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_DEPTH_TEST);
    gl.Enable(GL_SCISSOR_TEST);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.UseProgram(nkgl.prog);
    gl.Uniform1i(nkgl.u_tex, 0);
    const GLfloat ortho[16] = {
        2.0f / dw, 0.0f,       0.0f, 0.0f,
        0.0f,     -2.0f / dh,  0.0f, 0.0f,
        0.0f,      0.0f,      -1.0f, 0.0f,
       -1.0f,      1.0f,       0.0f, 1.0f,
    };
    gl.UniformMatrix4fv(nkgl.u_proj, 1, GL_FALSE, ortho);

    gl.BindVertexArray(nkgl.vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, nkgl.vbo);
    gl.BufferData(GL_ARRAY_BUFFER, (GLsizeiptr)vbuf.needed,
                  nk_buffer_memory_const(&vbuf), GL_STREAM_DRAW);
    gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, nkgl.ebo);
    gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)ebuf.needed,
                  nk_buffer_memory_const(&ebuf), GL_STREAM_DRAW);

    size_t offset = 0;
    const struct nk_draw_command *cmd;
    nk_draw_foreach(cmd, &sdl.ctx, &sdl.ogl.cmds) {
        if (!cmd->elem_count)
            continue;
        /* Nuklear's "no clip" rect reaches far off screen; GL wants the
         * scissor inside the drawable, counted from the bottom. */
        int x0 = (int)floorf(cmd->clip_rect.x);
        int y0 = (int)floorf(cmd->clip_rect.y);
        int x1 = (int)ceilf(cmd->clip_rect.x + cmd->clip_rect.w);
        int y1 = (int)ceilf(cmd->clip_rect.y + cmd->clip_rect.h);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > dw) x1 = dw;
        if (y1 > dh) y1 = dh;
        if (x1 < x0) x1 = x0;
        if (y1 < y0) y1 = y0;
        gl.Scissor(x0, dh - y1, x1 - x0, y1 - y0);
        gl.BindTexture(GL_TEXTURE_2D, (GLuint)cmd->texture.id);
        gl.DrawElements(GL_TRIANGLES, (GLsizei)cmd->elem_count, GL_UNSIGNED_INT,
                        (void *)offset);
        offset += cmd->elem_count * sizeof(nk_draw_index);
    }

    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_BLEND);
    gl.BindVertexArray(0);
    gl.UseProgram(0);

    nk_clear(&sdl.ctx);
    nk_buffer_clear(&sdl.ogl.cmds);
    nk_buffer_free(&vbuf);
    nk_buffer_free(&ebuf);
}

static void pg_nkgl_shutdown(void)
{
    gl.DeleteTextures(1, &nkgl.font);
    gl.DeleteBuffers(1, &nkgl.vbo);
    gl.DeleteBuffers(1, &nkgl.ebo);
    gl.DeleteVertexArrays(1, &nkgl.vao);
    gl.DeleteProgram(nkgl.prog);
    memset(&nkgl, 0, sizeof nkgl);

    nk_font_atlas_clear(&sdl.atlas);
    nk_free(&sdl.ctx);
    nk_buffer_free(&sdl.ogl.cmds);
    memset(&sdl, 0, sizeof sdl);
}

#endif /* PG_NKGL_H */
