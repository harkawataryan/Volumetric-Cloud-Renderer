#include "gl.h"
#include <cstdio>

#define VCR_DEF(type, name) type vcr_##name = nullptr;
VCR_GL_FUNCS(VCR_DEF)
#undef VCR_DEF

bool vcrLoadGL(VcrGetProc getProc) {
    bool ok = true;
#define VCR_LOAD(type, name) \
    vcr_##name = (type)getProc(#name); \
    if (!vcr_##name) { fprintf(stderr, "missing GL function: %s\n", #name); ok = false; }
    VCR_GL_FUNCS(VCR_LOAD)
#undef VCR_LOAD
    return ok;
}
