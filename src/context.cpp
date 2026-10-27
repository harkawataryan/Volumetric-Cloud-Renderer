#include "context.h"
#include "gl.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include <GLFW/glfw3.h>

namespace {

double nowSeconds() {
    using namespace std::chrono;
    static auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

struct WindowContext : Context {
    GLFWwindow* win = nullptr;
    std::unordered_map<int, bool> prev;
    double lastX = 0, lastY = 0;
    bool firstMouse = true;

    ~WindowContext() override {
        if (win) glfwDestroyWindow(win);
        glfwTerminate();
    }
    bool shouldClose() override { return glfwWindowShouldClose(win); }
    void pollEvents() override { glfwPollEvents(); }
    void swap() override { glfwSwapBuffers(win); }
    bool keyDown(int k) override { return glfwGetKey(win, k) == GLFW_PRESS; }
    bool keyPressed(int k) override {
        bool now = keyDown(k);
        bool was = prev[k];
        prev[k] = now;
        return now && !was;
    }
    void mouseDelta(float& dx, float& dy) override {
        double x, y;
        glfwGetCursorPos(win, &x, &y);
        if (firstMouse) { lastX = x; lastY = y; firstMouse = false; }
        dx = (float)(x - lastX);
        dy = (float)(y - lastY);
        lastX = x; lastY = y;
    }
    void setTitle(const std::string& t) override { glfwSetWindowTitle(win, t.c_str()); }
    void framebufferSize(int& w, int& h) override { glfwGetFramebufferSize(win, &w, &h); }
    double time() override { return glfwGetTime(); }
};

Context* createWindow(const ContextDesc& d) {
    if (!glfwInit()) {
        fprintf(stderr, "glfwInit failed (no display?). Use --headless for offscreen rendering.\n");
        return nullptr;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    GLFWwindow* win = glfwCreateWindow(d.width, d.height, d.title.c_str(), nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "glfwCreateWindow failed: this GPU/driver does not offer OpenGL 4.5 core\n");
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(0);
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (!vcrLoadGL((VcrGetProc)glfwGetProcAddress)) return nullptr;
    auto* c = new WindowContext();
    c->win = win;
    return c;
}

}  // namespace

#if defined(__linux__)
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace {

struct HeadlessContext : Context {
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    int w = 0, h = 0;

    ~HeadlessContext() override {
        if (dpy != EGL_NO_DISPLAY) {
            eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (ctx != EGL_NO_CONTEXT) eglDestroyContext(dpy, ctx);
            eglTerminate(dpy);
        }
    }
    bool shouldClose() override { return false; }
    void pollEvents() override {}
    void swap() override { glFinish(); }
    bool keyDown(int) override { return false; }
    bool keyPressed(int) override { return false; }
    void mouseDelta(float& dx, float& dy) override { dx = dy = 0; }
    void setTitle(const std::string&) override {}
    void framebufferSize(int& ow, int& oh) override { ow = w; oh = h; }
    double time() override { return nowSeconds(); }
};

Context* createHeadless(const ContextDesc& d) {
    auto getPlatformDisplay =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (getPlatformDisplay)
        dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    if (dpy == EGL_NO_DISPLAY) dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, nullptr, nullptr)) {
        fprintf(stderr, "EGL: no display. Install Mesa (libegl1, libgl1-mesa-dri) for headless rendering.\n");
        return nullptr;
    }
    eglBindAPI(EGL_OPENGL_API);
    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &n) || n == 0) {
        fprintf(stderr, "EGL: no config\n");
        return nullptr;
    }
    const EGLint ctxAttrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 5,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL: could not create a GL 4.5 core context\n");
        return nullptr;
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "EGL: surfaceless make-current failed\n");
        return nullptr;
    }
    if (!vcrLoadGL((VcrGetProc)eglGetProcAddress)) return nullptr;
    auto* c = new HeadlessContext();
    c->dpy = dpy;
    c->ctx = ctx;
    c->w = d.width;
    c->h = d.height;
    return c;
}

}  // namespace
#endif

Context* createContext(const ContextDesc& d) {
    if (d.headless) {
#if defined(__linux__)
        return createHeadless(d);
#else
        fprintf(stderr, "headless mode is only implemented on Linux (EGL surfaceless)\n");
        return nullptr;
#endif
    }
    return createWindow(d);
}
