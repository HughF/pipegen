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
 * pg_gl.h — the OpenGL 3.3 core functions pipegen uses, and nothing else
 *
 * No system GL headers and no GLEW: every function is looked up through
 * SDL_GL_GetProcAddress once a context exists. That is the only way that
 * works unchanged on Windows, where opengl32.dll exports GL 1.1 and the rest
 * comes from the driver, and it keeps the build free of GL link flags.
 *
 * Call as gl.Clear(GL_COLOR_BUFFER_BIT).
 */
#ifndef PG_GL_H
#define PG_GL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int  GLenum;
typedef unsigned int  GLuint;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef float         GLfloat;
typedef char          GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t     GLsizeiptr;
typedef ptrdiff_t     GLintptr;

#if defined(_WIN32) && !defined(_WIN64)
#define PG_APIENTRY __stdcall
#else
#define PG_APIENTRY
#endif

#define GL_FALSE                     0
#define GL_TRUE                      1
#define GL_NO_ERROR                  0
#define GL_TRIANGLES                 0x0004
#define GL_LESS                      0x0201
#define GL_SRC_ALPHA                 0x0302
#define GL_ONE_MINUS_SRC_ALPHA       0x0303
#define GL_ZERO                      0
#define GL_ONE                       1
#define GL_FUNC_ADD                  0x8006
#define GL_CULL_FACE                 0x0B44
#define GL_DEPTH_TEST                0x0B71
#define GL_BLEND                     0x0BE2
#define GL_SCISSOR_TEST              0x0C11
#define GL_UNPACK_ALIGNMENT          0x0CF5
#define GL_PACK_ALIGNMENT            0x0D05
#define GL_TEXTURE_2D                0x0DE1
#define GL_UNSIGNED_BYTE             0x1401
#define GL_INT                       0x1404
#define GL_UNSIGNED_INT              0x1405
#define GL_FLOAT                     0x1406
#define GL_RGBA                      0x1908
#define GL_VENDOR                    0x1F00
#define GL_RENDERER                  0x1F01
#define GL_VERSION                   0x1F02
#define GL_NEAREST                   0x2600
#define GL_LINEAR                    0x2601
#define GL_TEXTURE_MAG_FILTER        0x2800
#define GL_TEXTURE_MIN_FILTER        0x2801
#define GL_TEXTURE_WRAP_S            0x2802
#define GL_TEXTURE_WRAP_T            0x2803
#define GL_CLAMP_TO_EDGE             0x812F
#define GL_RGBA8                     0x8058
#define GL_DEPTH_COMPONENT24         0x81A6
#define GL_TEXTURE0                  0x84C0
#define GL_ARRAY_BUFFER              0x8892
#define GL_ELEMENT_ARRAY_BUFFER      0x8893
#define GL_STREAM_DRAW               0x88E0
#define GL_STATIC_DRAW               0x88E4
#define GL_FRAGMENT_SHADER           0x8B30
#define GL_VERTEX_SHADER             0x8B31
#define GL_COMPILE_STATUS            0x8B81
#define GL_LINK_STATUS               0x8B82
#define GL_READ_FRAMEBUFFER          0x8CA8
#define GL_DRAW_FRAMEBUFFER          0x8CA9
#define GL_FRAMEBUFFER_COMPLETE      0x8CD5
#define GL_MAX_SAMPLES               0x8D57
#define GL_COLOR_ATTACHMENT0         0x8CE0
#define GL_DEPTH_ATTACHMENT          0x8D00
#define GL_FRAMEBUFFER               0x8D40
#define GL_RENDERBUFFER              0x8D41
#define GL_COLOR_BUFFER_BIT          0x00004000
#define GL_DEPTH_BUFFER_BIT          0x00000100

#define PG_GL_FUNCS \
    X(void,          ActiveTexture,       (GLenum)) \
    X(void,          AttachShader,        (GLuint, GLuint)) \
    X(void,          BindBuffer,          (GLenum, GLuint)) \
    X(void,          BindFramebuffer,     (GLenum, GLuint)) \
    X(void,          BindRenderbuffer,    (GLenum, GLuint)) \
    X(void,          BindTexture,         (GLenum, GLuint)) \
    X(void,          BindVertexArray,     (GLuint)) \
    X(void,          BlendEquation,       (GLenum)) \
    X(void,          BlendFunc,           (GLenum, GLenum)) \
    X(void,          BlendFuncSeparate,   (GLenum, GLenum, GLenum, GLenum)) \
    X(void,          BlitFramebuffer,     (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void,          BufferData,          (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(GLenum,        CheckFramebufferStatus, (GLenum)) \
    X(void,          Clear,               (GLbitfield)) \
    X(void,          ClearColor,          (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void,          CompileShader,       (GLuint)) \
    X(GLuint,        CreateProgram,       (void)) \
    X(GLuint,        CreateShader,        (GLenum)) \
    X(void,          DeleteBuffers,       (GLsizei, const GLuint *)) \
    X(void,          DeleteFramebuffers,  (GLsizei, const GLuint *)) \
    X(void,          DeleteProgram,       (GLuint)) \
    X(void,          DeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void,          DeleteShader,        (GLuint)) \
    X(void,          DeleteTextures,      (GLsizei, const GLuint *)) \
    X(void,          DeleteVertexArrays,  (GLsizei, const GLuint *)) \
    X(void,          DepthFunc,           (GLenum)) \
    X(void,          DepthMask,           (GLboolean)) \
    X(void,          Disable,             (GLenum)) \
    X(void,          DrawArrays,          (GLenum, GLint, GLsizei)) \
    X(void,          DrawElements,        (GLenum, GLsizei, GLenum, const void *)) \
    X(void,          Enable,              (GLenum)) \
    X(void,          EnableVertexAttribArray, (GLuint)) \
    X(void,          FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(void,          FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void,          GenBuffers,          (GLsizei, GLuint *)) \
    X(void,          GenFramebuffers,     (GLsizei, GLuint *)) \
    X(void,          GenRenderbuffers,    (GLsizei, GLuint *)) \
    X(void,          GenTextures,         (GLsizei, GLuint *)) \
    X(void,          GenVertexArrays,     (GLsizei, GLuint *)) \
    X(GLenum,        GetError,            (void)) \
    X(void,          GetIntegerv,         (GLenum, GLint *)) \
    X(void,          GetProgramInfoLog,   (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,          GetProgramiv,        (GLuint, GLenum, GLint *)) \
    X(void,          GetShaderInfoLog,    (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void,          GetShaderiv,         (GLuint, GLenum, GLint *)) \
    X(const GLubyte *, GetString,         (GLenum)) \
    X(GLint,         GetUniformLocation,  (GLuint, const GLchar *)) \
    X(void,          LinkProgram,         (GLuint)) \
    X(void,          PixelStorei,         (GLenum, GLint)) \
    X(void,          ReadPixels,          (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void,          RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(void,          RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void,          Scissor,             (GLint, GLint, GLsizei, GLsizei)) \
    X(void,          ShaderSource,        (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void,          TexImage2D,          (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void,          TexParameteri,       (GLenum, GLenum, GLint)) \
    X(void,          Uniform1f,           (GLint, GLfloat)) \
    X(void,          Uniform1i,           (GLint, GLint)) \
    X(void,          Uniform2f,           (GLint, GLfloat, GLfloat)) \
    X(void,          Uniform3f,           (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void,          UniformMatrix4fv,    (GLint, GLsizei, GLboolean, const GLfloat *)) \
    X(void,          UseProgram,          (GLuint)) \
    X(void,          VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *)) \
    X(void,          VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void,          Viewport,            (GLint, GLint, GLsizei, GLsizei))

typedef struct {
#define X(ret, name, args) ret (PG_APIENTRY *name) args;
    PG_GL_FUNCS
#undef X
} PgGl;

extern PgGl gl;

/* Look every function up in the current context and check it is at least
 * OpenGL 3.3. False, with the reason in err, if not. */
bool   pg_gl_load(char *err, size_t cap);

/* "AMD Radeon Graphics, OpenGL 4.6 (Core Profile) Mesa 26.1.1" */
void   pg_gl_describe(char *buf, size_t cap);

/* Compile and link a program. 0, with the compiler's log in err, on failure. */
GLuint pg_gl_program(const char *vs, const char *fs, char *err, size_t cap);

#endif /* PG_GL_H */
