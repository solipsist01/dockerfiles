#include "gl.h"

#define X(ret, name, params) PFN_##name name = nullptr;
GL_FUNC_LIST(X)
#undef X

static void* getProc(const char* n) {
    void* p = (void*)wglGetProcAddress(n);
    if (p == nullptr || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE lib = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(lib, n);
    }
    return p;
}

bool loadGLFunctions() {
    bool ok = true;
#define X(ret, name, params) name = (PFN_##name)getProc(#name); if (!name) ok = false;
    GL_FUNC_LIST(X)
#undef X
    return ok;
}
