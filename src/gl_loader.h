// SPDX-License-Identifier: Apache-2.0
// Tiny OpenGL 3.3 function loader. OpenGL 1.1 functions come straight from the system
// header / library; everything newer is loaded at runtime into the mi::gl namespace.
#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>

#include <cstddef>

#ifndef APIENTRY
#define APIENTRY
#endif

// Types and constants that the system gl.h does not provide (Windows only ships GL 1.1).
#ifndef GL_VERSION_1_5
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#endif
#ifndef GL_VERSION_2_0
typedef char GLchar;
#endif

namespace mi::gl {

// constants that GL 1.1 headers lack (kept in the mi::gl namespace so they can never clash)
constexpr GLenum ARRAY_BUFFER = 0x8892;
constexpr GLenum ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum STATIC_DRAW = 0x88E4;
constexpr GLenum FRAGMENT_SHADER = 0x8B30;
constexpr GLenum VERTEX_SHADER = 0x8B31;
constexpr GLenum COMPILE_STATUS = 0x8B81;
constexpr GLenum LINK_STATUS = 0x8B82;
constexpr GLenum INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum PROGRAM_POINT_SIZE = 0x8642;
constexpr GLenum MULTISAMPLE = 0x809D;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum RGBA32F = 0x8814;
constexpr GLenum CLAMP_TO_EDGE = 0x812F;

#define MI_GL_FUNCTIONS(X)                                                                         \
    X(GLuint, CreateShader, (GLenum type))                                                         \
    X(void, ShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, CompileShader, (GLuint shader))                                                        \
    X(void, GetShaderiv, (GLuint shader, GLenum pname, GLint* params))                             \
    X(void, GetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog))  \
    X(GLuint, CreateProgram, ())                                                                   \
    X(void, AttachShader, (GLuint program, GLuint shader))                                         \
    X(void, LinkProgram, (GLuint program))                                                         \
    X(void, GetProgramiv, (GLuint program, GLenum pname, GLint* params))                           \
    X(void, GetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, DeleteShader, (GLuint shader))                                                         \
    X(void, DeleteProgram, (GLuint program))                                                       \
    X(void, UseProgram, (GLuint program))                                                          \
    X(GLint, GetUniformLocation, (GLuint program, const GLchar* name))                             \
    X(void, Uniform1i, (GLint location, GLint v0))                                                 \
    X(void, Uniform1f, (GLint location, GLfloat v0))                                               \
    X(void, Uniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))                       \
    X(void, Uniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))           \
    X(void, UniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, UniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, ActiveTexture, (GLenum texture))                                                       \
    X(void, GenVertexArrays, (GLsizei n, GLuint* arrays))                                          \
    X(void, BindVertexArray, (GLuint array))                                                       \
    X(void, DeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                 \
    X(void, GenBuffers, (GLsizei n, GLuint* buffers))                                              \
    X(void, BindBuffer, (GLenum target, GLuint buffer))                                            \
    X(void, BufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))          \
    X(void, DeleteBuffers, (GLsizei n, const GLuint* buffers))                                     \
    X(void, EnableVertexAttribArray, (GLuint index))                                               \
    X(void, VertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer))

#define MI_GL_DECLARE(ret, name, args) extern ret(APIENTRY* name) args;
MI_GL_FUNCTIONS(MI_GL_DECLARE)
#undef MI_GL_DECLARE

using GetProc = void* (*)(const char*);

// Loads all functions above using `getProc` (e.g. glfwGetProcAddress). Returns false and
// names the first missing function in `missing` if the driver does not provide OpenGL 3.3.
bool load(GetProc getProc, const char** missing);

}  // namespace mi::gl
