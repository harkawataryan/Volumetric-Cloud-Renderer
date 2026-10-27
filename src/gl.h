// Minimal OpenGL 4.5 core loader. Function pointers are resolved through
// whatever the context provides (glfwGetProcAddress or eglGetProcAddress),
// so the renderer never links against a platform GL library directly.
#pragma once
#include "../third_party/glcorearb.h"

#define VCR_GL_FUNCS(X) \
    X(PFNGLGETSTRINGPROC, glGetString) \
    X(PFNGLGETINTEGERVPROC, glGetIntegerv) \
    X(PFNGLENABLEPROC, glEnable) \
    X(PFNGLDISABLEPROC, glDisable) \
    X(PFNGLVIEWPORTPROC, glViewport) \
    X(PFNGLCLEARCOLORPROC, glClearColor) \
    X(PFNGLCLEARPROC, glClear) \
    X(PFNGLFINISHPROC, glFinish) \
    X(PFNGLPIXELSTOREIPROC, glPixelStorei) \
    X(PFNGLREADPIXELSPROC, glReadPixels) \
    X(PFNGLGENTEXTURESPROC, glGenTextures) \
    X(PFNGLDELETETEXTURESPROC, glDeleteTextures) \
    X(PFNGLBINDTEXTUREPROC, glBindTexture) \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture) \
    X(PFNGLTEXSTORAGE2DPROC, glTexStorage2D) \
    X(PFNGLTEXSTORAGE3DPROC, glTexStorage3D) \
    X(PFNGLTEXPARAMETERIPROC, glTexParameteri) \
    X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap) \
    X(PFNGLBINDIMAGETEXTUREPROC, glBindImageTexture) \
    X(PFNGLCREATESHADERPROC, glCreateShader) \
    X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) \
    X(PFNGLDELETESHADERPROC, glDeleteShader) \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) \
    X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLDELETEPROGRAMPROC, glDeleteProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) \
    X(PFNGLUNIFORM1FPROC, glUniform1f) \
    X(PFNGLUNIFORM2FPROC, glUniform2f) \
    X(PFNGLUNIFORM3FPROC, glUniform3f) \
    X(PFNGLUNIFORM4FPROC, glUniform4f) \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
    X(PFNGLDISPATCHCOMPUTEPROC, glDispatchCompute) \
    X(PFNGLMEMORYBARRIERPROC, glMemoryBarrier) \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers) \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers) \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D) \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus) \
    X(PFNGLDRAWBUFFERSPROC, glDrawBuffers) \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
    X(PFNGLDRAWARRAYSPROC, glDrawArrays) \
    X(PFNGLGENQUERIESPROC, glGenQueries) \
    X(PFNGLQUERYCOUNTERPROC, glQueryCounter) \
    X(PFNGLGETQUERYOBJECTUI64VPROC, glGetQueryObjectui64v)

#define VCR_DECL(type, name) extern type vcr_##name;
VCR_GL_FUNCS(VCR_DECL)
#undef VCR_DECL

#define glGetString vcr_glGetString
#define glGetIntegerv vcr_glGetIntegerv
#define glEnable vcr_glEnable
#define glDisable vcr_glDisable
#define glViewport vcr_glViewport
#define glClearColor vcr_glClearColor
#define glClear vcr_glClear
#define glFinish vcr_glFinish
#define glPixelStorei vcr_glPixelStorei
#define glReadPixels vcr_glReadPixels
#define glGenTextures vcr_glGenTextures
#define glDeleteTextures vcr_glDeleteTextures
#define glBindTexture vcr_glBindTexture
#define glActiveTexture vcr_glActiveTexture
#define glTexStorage2D vcr_glTexStorage2D
#define glTexStorage3D vcr_glTexStorage3D
#define glTexParameteri vcr_glTexParameteri
#define glGenerateMipmap vcr_glGenerateMipmap
#define glBindImageTexture vcr_glBindImageTexture
#define glCreateShader vcr_glCreateShader
#define glShaderSource vcr_glShaderSource
#define glCompileShader vcr_glCompileShader
#define glGetShaderiv vcr_glGetShaderiv
#define glGetShaderInfoLog vcr_glGetShaderInfoLog
#define glDeleteShader vcr_glDeleteShader
#define glCreateProgram vcr_glCreateProgram
#define glAttachShader vcr_glAttachShader
#define glLinkProgram vcr_glLinkProgram
#define glGetProgramiv vcr_glGetProgramiv
#define glGetProgramInfoLog vcr_glGetProgramInfoLog
#define glUseProgram vcr_glUseProgram
#define glDeleteProgram vcr_glDeleteProgram
#define glGetUniformLocation vcr_glGetUniformLocation
#define glUniform1i vcr_glUniform1i
#define glUniform1f vcr_glUniform1f
#define glUniform2f vcr_glUniform2f
#define glUniform3f vcr_glUniform3f
#define glUniform4f vcr_glUniform4f
#define glUniformMatrix4fv vcr_glUniformMatrix4fv
#define glDispatchCompute vcr_glDispatchCompute
#define glMemoryBarrier vcr_glMemoryBarrier
#define glGenFramebuffers vcr_glGenFramebuffers
#define glDeleteFramebuffers vcr_glDeleteFramebuffers
#define glBindFramebuffer vcr_glBindFramebuffer
#define glFramebufferTexture2D vcr_glFramebufferTexture2D
#define glCheckFramebufferStatus vcr_glCheckFramebufferStatus
#define glDrawBuffers vcr_glDrawBuffers
#define glGenVertexArrays vcr_glGenVertexArrays
#define glBindVertexArray vcr_glBindVertexArray
#define glDrawArrays vcr_glDrawArrays
#define glGenQueries vcr_glGenQueries
#define glQueryCounter vcr_glQueryCounter
#define glGetQueryObjectui64v vcr_glGetQueryObjectui64v

typedef void* (*VcrGetProc)(const char* name);

// Resolves every function above. Returns false and prints the first missing
// symbol if the context does not expose GL 4.3+ (compute, texture storage).
bool vcrLoadGL(VcrGetProc getProc);
