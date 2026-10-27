// Two ways to get a GL 4.5 context: a GLFW window for interactive use, or a
// surfaceless EGL context (Linux) for headless rendering in CI and benchmarks.
#pragma once
#include <functional>
#include <string>

struct ContextDesc {
    int width = 1280;
    int height = 720;
    bool headless = false;
    std::string title = "Volumetric Clouds";
};

struct Context {
    virtual ~Context() = default;
    virtual bool shouldClose() = 0;
    virtual void pollEvents() = 0;
    virtual void swap() = 0;
    virtual bool keyDown(int glfwKey) = 0;
    virtual bool keyPressed(int glfwKey) = 0;   // edge-triggered
    virtual void mouseDelta(float& dx, float& dy) = 0;
    virtual void setTitle(const std::string& t) = 0;
    virtual void framebufferSize(int& w, int& h) = 0;
    virtual double time() = 0;
};

// Creates the context and loads GL. Returns nullptr with a message on stderr
// if no suitable context is available.
Context* createContext(const ContextDesc& desc);
