/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2021      Rinnegatamante
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  glutil.h
 * @brief OpenGL API initializer, related functions.
 */

#ifndef SOLOADER_GLUTIL_H
#define SOLOADER_GLUTIL_H

#include <vitaGL.h>

#ifdef __cplusplus
extern "C" {
#endif

void gl_init();

void gl_preload();

void gl_swap();
void glTexImage2D_profiled(GLenum target, GLint level, GLint internal_format,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const void *pixels);
void glCompressedTexImage2D_profiled(GLenum target, GLint level, GLenum internal_format,
                         GLsizei width, GLsizei height, GLint border,
                         GLsizei image_size, const void *pixels);

void glCompileShader_soloader(GLuint shader);

void glShaderSource_soloader(GLuint shader, GLsizei count,
                             const GLchar **string, const GLint *_length);

void glDisable_soloader(GLenum cap);

void glEnableClientState_soloader(GLenum array);

void glDisableClientState_soloader(GLenum array);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_GLUTIL_H
