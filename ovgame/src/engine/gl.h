#pragma once
// Eigen OpenGL-loader: OpenGL 1.1 komt uit opengl32.dll, alles daarboven laden we zelf in via wglGetProcAddress.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define GL_GLEXT_LEGACY
#include <windows.h>
#include <GL/gl.h>
#include <cstddef>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_BGRA 0x80E1
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_R8 0x8229
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_SAMPLES 0x8D57
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#define GL_RGBA32F 0x8814
#define GL_TEXTURE_BASE_LEVEL 0x813C
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5

#define GL_FUNC_LIST(X) \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint s, GLsizei n, const GLchar* const* str, const GLint* len)) \
    X(void, glCompileShader, (GLuint s)) \
    X(void, glGetShaderiv, (GLuint s, GLenum p, GLint* v)) \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei* len, GLchar* log)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint p, GLuint s)) \
    X(void, glLinkProgram, (GLuint p)) \
    X(void, glGetProgramiv, (GLuint p, GLenum e, GLint* v)) \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei* len, GLchar* log)) \
    X(void, glDeleteShader, (GLuint s)) \
    X(void, glUseProgram, (GLuint p)) \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar* name)) \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei n, GLboolean t, const GLfloat* v)) \
    X(void, glUniform4f, (GLint loc, GLfloat a, GLfloat b, GLfloat c, GLfloat d)) \
    X(void, glUniform3f, (GLint loc, GLfloat a, GLfloat b, GLfloat c)) \
    X(void, glUniform2f, (GLint loc, GLfloat a, GLfloat b)) \
    X(void, glUniform1f, (GLint loc, GLfloat a)) \
    X(void, glUniform1i, (GLint loc, GLint a)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* a)) \
    X(void, glBindVertexArray, (GLuint a)) \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* a)) \
    X(void, glGenBuffers, (GLsizei n, GLuint* b)) \
    X(void, glBindBuffer, (GLenum t, GLuint b)) \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* b)) \
    X(void, glBufferData, (GLenum t, GLsizeiptr size, const void* data, GLenum usage)) \
    X(void, glVertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* ptr)) \
    X(void, glEnableVertexAttribArray, (GLuint i)) \
    X(void, glActiveTexture, (GLenum t)) \
    X(void, glTexImage3D, (GLenum t, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum fmt, GLenum type, const void* px)) \
    X(void, glTexSubImage3D, (GLenum t, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum fmt, GLenum type, const void* px)) \
    X(void, glGenerateMipmap, (GLenum t)) \
    X(void, glCompressedTexSubImage3D, (GLenum t, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d, GLenum fmt, GLsizei size, const void* data)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* f)) \
    X(void, glBindFramebuffer, (GLenum t, GLuint f)) \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* f)) \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint* r)) \
    X(void, glBindRenderbuffer, (GLenum t, GLuint r)) \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* r)) \
    X(void, glRenderbufferStorageMultisample, (GLenum t, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h)) \
    X(void, glFramebufferRenderbuffer, (GLenum t, GLenum att, GLenum rbt, GLuint rb)) \
    X(void, glFramebufferTexture2D, (GLenum t, GLenum att, GLenum textarget, GLuint tex, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum t)) \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

#define X(ret, name, params) typedef ret(APIENTRY* PFN_##name) params; extern PFN_##name name;
GL_FUNC_LIST(X)
#undef X

bool loadGLFunctions();
