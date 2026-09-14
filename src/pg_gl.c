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
#include "pg_gl.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

PgGl gl;

bool pg_gl_load(char *err, size_t cap)
{
    memset(&gl, 0, sizeof gl);
#define X(ret, name, args)                                                  \
    gl.name = (ret (PG_APIENTRY *) args)SDL_GL_GetProcAddress("gl" #name);  \
    if (!gl.name) {                                                         \
        snprintf(err, cap, "the OpenGL driver has no gl" #name);            \
        return false;                                                       \
    }
    PG_GL_FUNCS
#undef X

    const char *ver = (const char *)gl.GetString(GL_VERSION);
    int major = 0, minor = 0;
    if (!ver || sscanf(ver, "%d.%d", &major, &minor) != 2) {
        snprintf(err, cap, "the OpenGL driver reports no version");
        return false;
    }
    if (major < 3 || (major == 3 && minor < 3)) {
        snprintf(err, cap, "OpenGL %d.%d; 3.3 is needed", major, minor);
        return false;
    }
    return true;
}

/* Up to the first " (": drivers append build details that do not fit a row. */
static int short_len(const char *s)
{
    const char *p = strstr(s, " (");
    return p ? (int)(p - s) : (int)strlen(s);
}

void pg_gl_describe(char *buf, size_t cap)
{
    const char *ren = gl.GetString ? (const char *)gl.GetString(GL_RENDERER) : NULL;
    const char *ver = gl.GetString ? (const char *)gl.GetString(GL_VERSION) : NULL;
    if (!ren) ren = "unknown";
    if (!ver) ver = "?";
    snprintf(buf, cap, "%.*s, OpenGL %.*s", short_len(ren), ren, short_len(ver), ver);
}

static GLuint shader(GLenum kind, const char *src, char *err, size_t cap)
{
    GLuint s = gl.CreateShader(kind);
    gl.ShaderSource(s, 1, &src, NULL);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        gl.GetShaderInfoLog(s, sizeof log, NULL, log);
        snprintf(err, cap, "%s shader: %s",
                 kind == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        gl.DeleteShader(s);
        return 0;
    }
    return s;
}

GLuint pg_gl_program(const char *vs, const char *fs, char *err, size_t cap)
{
    GLuint v = shader(GL_VERTEX_SHADER, vs, err, cap);
    if (!v)
        return 0;
    GLuint f = shader(GL_FRAGMENT_SHADER, fs, err, cap);
    if (!f) {
        gl.DeleteShader(v);
        return 0;
    }
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, v);
    gl.AttachShader(p, f);
    gl.LinkProgram(p);
    gl.DeleteShader(v);
    gl.DeleteShader(f);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = "";
        gl.GetProgramInfoLog(p, sizeof log, NULL, log);
        snprintf(err, cap, "shader link: %s", log);
        gl.DeleteProgram(p);
        return 0;
    }
    return p;
}
