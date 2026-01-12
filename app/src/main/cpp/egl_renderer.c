#include "egl_renderer.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>

void egl_init(void) {
    // stub for now
}

void egl_clear(float r, float g, float b) {
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void egl_swap(void) {
    // swap handled later
}
