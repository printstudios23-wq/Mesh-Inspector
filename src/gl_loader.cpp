// SPDX-License-Identifier: Apache-2.0
#include "gl_loader.h"

namespace mi::gl {

#define MI_GL_DEFINE_PTR(ret, name, args) ret(APIENTRY* name) args = nullptr;
MI_GL_FUNCTIONS(MI_GL_DEFINE_PTR)
#undef MI_GL_DEFINE_PTR

bool load(GetProc getProc, const char** missing) {
    bool ok = true;
#define MI_GL_LOAD(ret, name, args)                                                  \
    if (ok) {                                                                        \
        name = reinterpret_cast<decltype(name)>(getProc("gl" #name));                \
        if (!name) {                                                                 \
            ok = false;                                                              \
            if (missing) *missing = "gl" #name;                                      \
        }                                                                            \
    }
    MI_GL_FUNCTIONS(MI_GL_LOAD)
#undef MI_GL_LOAD
    return ok;
}

}  // namespace mi::gl
