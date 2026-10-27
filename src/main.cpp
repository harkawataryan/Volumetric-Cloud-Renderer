// Volumetric cloud renderer. Interactive by default; --headless renders to a
// PNG without a display, --bench prints per-pass GPU timings for a few
// configurations.
#include "context.h"
#include "gl.h"
#include "renderer.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

namespace {

struct Options {
    bool headless = false;
    bool bench = false;
    int width = 1280, height = 720;
    int frames = 48;
    std::string out = "clouds.png";
    Vec3 move;   // camera translation per frame in headless mode
    std::string shaderDir = "shaders";
    RenderSettings settings;
    Weather weather;
    Camera camera;
};

void usage() {
    printf(
        "usage: clouds [options]\n"
        "  --headless            render offscreen (Linux, Mesa EGL) and write --out\n"
        "  --out FILE            PNG path for headless mode (default clouds.png)\n"
        "  --frames N            frames to accumulate before writing (default 48)\n"
        "  --size WxH            output resolution (default 1280x720)\n"
        "  --scale F             cloud pass resolution scale (default 0.5)\n"
        "  --steps N             max march steps (default 128)\n"
        "  --no-temporal         disable reprojection/accumulation\n"
        "  --coverage F --density F --type F --sun-el F --sun-az F\n"
        "  --sun-intensity F --extinction F --ambient F --erosion F   look tuning\n"
        "  --cam X,Y,Z --yaw F --pitch F\n"
        "  --move X,Y,Z          headless: move the camera this much every frame\n"
        "  --bench               time each pass for several configurations and exit\n"
        "  --shaders DIR         shader directory (default ./shaders)\n");
}

bool parse(int argc, char** argv, Options& o) {
    auto next = [&](int& i) -> const char* {
        if (i + 1 >= argc) { usage(); exit(1); }
        return argv[++i];
    };
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") o.headless = true;
        else if (a == "--bench") {
            o.bench = true;
#if defined(__linux__)
            o.headless = true;   // no display needed
#endif
        }
        else if (a == "--out") o.out = next(i);
        else if (a == "--frames") o.frames = atoi(next(i));
        else if (a == "--size") sscanf(next(i), "%dx%d", &o.width, &o.height);
        else if (a == "--scale") o.settings.scale = (float)atof(next(i));
        else if (a == "--steps") o.settings.maxSteps = atoi(next(i));
        else if (a == "--no-temporal") o.settings.temporal = false;
        else if (a == "--coverage") o.weather.coverage = (float)atof(next(i));
        else if (a == "--density") o.weather.density = (float)atof(next(i));
        else if (a == "--type") o.weather.cloudType = (float)atof(next(i));
        else if (a == "--sun-el") o.weather.sunElevation = (float)atof(next(i));
        else if (a == "--sun-az") o.weather.sunAzimuth = (float)atof(next(i));
        else if (a == "--sun-intensity") o.weather.sunIntensity = (float)atof(next(i));
        else if (a == "--extinction") o.weather.extinction = (float)atof(next(i));
        else if (a == "--ambient") o.weather.ambient = (float)atof(next(i));
        else if (a == "--move") sscanf(next(i), "%f,%f,%f", &o.move.x, &o.move.y, &o.move.z);
        else if (a == "--erosion") o.weather.erosion = (float)atof(next(i));
        else if (a == "--cam") sscanf(next(i), "%f,%f,%f", &o.camera.pos.x, &o.camera.pos.y, &o.camera.pos.z);
        else if (a == "--yaw") o.camera.yaw = (float)atof(next(i));
        else if (a == "--pitch") o.camera.pitch = (float)atof(next(i));
        else if (a == "--shaders") o.shaderDir = next(i);
        else if (a == "--help" || a == "-h") { usage(); exit(0); }
        else { fprintf(stderr, "unknown option %s\n", argv[i]); usage(); return false; }
    }
    return true;
}

bool writePng(const std::string& path, Renderer& r) {
    int w, h;
    auto px = r.readback(w, h);
    if (!stbi_write_png(path.c_str(), w, h, 3, px.data(), w * 3)) {
        fprintf(stderr, "failed to write %s\n", path.c_str());
        return false;
    }
    printf("wrote %s (%dx%d)\n", path.c_str(), w, h);
    return true;
}

int runHeadless(Options& o, Context& ctx, Renderer& r) {
    for (int i = 0; i < o.frames; ++i) {
        r.render(o.camera, o.weather, o.settings, i / 60.0f);
        ctx.swap();
        o.camera.pos += o.move;
    }
    auto t = r.timings();
    printf("last frame: clouds %.2f ms, resolve %.2f ms, composite %.2f ms, total %.2f ms\n",
           t.cloudMs, t.resolveMs, t.compositeMs, t.totalMs);
    return writePng(o.out, r) ? 0 : 1;
}

// Average GPU time per pass over a window of frames for one configuration.
FrameTimings timeConfig(Renderer& r, Context& ctx, const Options& o, const RenderSettings& s, int frames) {
    r.resetHistory();
    FrameTimings acc;
    for (int i = 0; i < frames + 8; ++i) {
        r.render(o.camera, o.weather, s, 0.0f);
        ctx.swap();
        if (i >= 8) {
            auto t = r.timings();
            acc.cloudMs += t.cloudMs; acc.resolveMs += t.resolveMs;
            acc.compositeMs += t.compositeMs; acc.totalMs += t.totalMs;
        }
    }
    acc.cloudMs /= frames; acc.resolveMs /= frames; acc.compositeMs /= frames; acc.totalMs /= frames;
    return acc;
}

int runBench(Options& o, Context& ctx, Renderer& r) {
    struct Cfg { const char* name; float scale; bool temporal; int steps; };
    const Cfg cfgs[] = {
        {"full-res, 128 steps, no temporal", 1.0f, false, 128},
        {"full-res, 64 steps, no temporal", 1.0f, false, 64},
        {"half-res, 128 steps, temporal", 0.5f, true, 128},
        {"quarter-res, 128 steps, temporal", 0.25f, true, 128},
    };
    printf("%dx%d, %s\n", o.width, o.height, (const char*)glGetString(GL_RENDERER));
    printf("%-38s %10s %10s %10s %10s\n", "configuration", "clouds ms", "resolve", "composite", "total");
    for (const Cfg& c : cfgs) {
        RenderSettings s = o.settings;
        s.scale = c.scale; s.temporal = c.temporal; s.maxSteps = c.steps;
        FrameTimings t = timeConfig(r, ctx, o, s, o.frames);
        printf("%-38s %10.2f %10.2f %10.2f %10.2f\n", c.name, t.cloudMs, t.resolveMs, t.compositeMs, t.totalMs);
    }
    return 0;
}

int runInteractive(Options& o, Context& ctx, Renderer& r) {
    double last = ctx.time();
    double fpsAccum = 0;
    int fpsFrames = 0;
    bool paused = false;
    float simTime = 0.f;
    while (!ctx.shouldClose()) {
        ctx.pollEvents();
        double now = ctx.time();
        float dt = (float)(now - last);
        last = now;
        if (!paused) simTime += dt;

        int w, h;
        ctx.framebufferSize(w, h);
        if (w != o.width || h != o.height) { o.width = w; o.height = h; r.resize(w, h); }

        float dx, dy;
        ctx.mouseDelta(dx, dy);
        o.camera.yaw += dx * 0.0025f;
        o.camera.pitch = std::max(-1.5f, std::min(1.5f, o.camera.pitch - dy * 0.0025f));

        float speed = (ctx.keyDown(GLFW_KEY_LEFT_SHIFT) ? 2000.f : 400.f) * dt;
        Vec3 fwd = o.camera.forward();
        Vec3 right = normalize(cross(fwd, Vec3(0, 1, 0)));
        if (ctx.keyDown(GLFW_KEY_W)) o.camera.pos += fwd * speed;
        if (ctx.keyDown(GLFW_KEY_S)) o.camera.pos += fwd * -speed;
        if (ctx.keyDown(GLFW_KEY_A)) o.camera.pos += right * -speed;
        if (ctx.keyDown(GLFW_KEY_D)) o.camera.pos += right * speed;
        if (ctx.keyDown(GLFW_KEY_Q)) o.camera.pos.y -= speed;
        if (ctx.keyDown(GLFW_KEY_E)) o.camera.pos.y += speed;

        float k = dt * 0.4f;
        if (ctx.keyDown(GLFW_KEY_Z)) o.weather.coverage = std::max(0.f, o.weather.coverage - k);
        if (ctx.keyDown(GLFW_KEY_X)) o.weather.coverage = std::min(1.f, o.weather.coverage + k);
        if (ctx.keyDown(GLFW_KEY_C)) o.weather.density = std::max(0.1f, o.weather.density - k);
        if (ctx.keyDown(GLFW_KEY_V)) o.weather.density = std::min(3.f, o.weather.density + k);
        if (ctx.keyDown(GLFW_KEY_B)) o.weather.cloudType = std::max(0.f, o.weather.cloudType - k);
        if (ctx.keyDown(GLFW_KEY_N)) o.weather.cloudType = std::min(1.f, o.weather.cloudType + k);
        if (ctx.keyDown(GLFW_KEY_O)) o.weather.sunAzimuth -= k;
        if (ctx.keyDown(GLFW_KEY_P)) o.weather.sunAzimuth += k;
        if (ctx.keyDown(GLFW_KEY_K)) o.weather.sunElevation = std::max(-0.1f, o.weather.sunElevation - k * 0.5f);
        if (ctx.keyDown(GLFW_KEY_L)) o.weather.sunElevation = std::min(1.5f, o.weather.sunElevation + k * 0.5f);
        if (ctx.keyPressed(GLFW_KEY_T)) { o.settings.temporal = !o.settings.temporal; r.resetHistory(); }
        if (ctx.keyPressed(GLFW_KEY_1)) o.settings.scale = 1.0f;
        if (ctx.keyPressed(GLFW_KEY_2)) o.settings.scale = 0.5f;
        if (ctx.keyPressed(GLFW_KEY_3)) o.settings.scale = 0.25f;
        if (ctx.keyPressed(GLFW_KEY_SPACE)) paused = !paused;
        if (ctx.keyPressed(GLFW_KEY_H)) o.settings.showNoise = !o.settings.showNoise;
        if (ctx.keyPressed(GLFW_KEY_R)) o.weather = Weather();
        if (ctx.keyPressed(GLFW_KEY_F5)) { if (r.reloadShaders()) printf("shaders reloaded\n"); }
        if (ctx.keyPressed(GLFW_KEY_F12)) writePng("screenshot.png", r);
        if (ctx.keyDown(GLFW_KEY_ESCAPE)) break;

        r.render(o.camera, o.weather, o.settings, simTime);
        ctx.swap();

        fpsAccum += dt;
        if (++fpsFrames >= 30) {
            auto t = r.timings();
            char title[256];
            snprintf(title, sizeof title,
                     "Volumetric Clouds  %.1f fps | clouds %.2f ms resolve %.2f ms | scale %.2f %s | cov %.2f dens %.2f type %.2f",
                     fpsFrames / fpsAccum, t.cloudMs, t.resolveMs, o.settings.scale,
                     o.settings.temporal ? "temporal" : "no-temporal",
                     o.weather.coverage, o.weather.density, o.weather.cloudType);
            ctx.setTitle(title);
            fpsAccum = 0;
            fpsFrames = 0;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) return 1;

    ContextDesc d;
    d.width = o.width;
    d.height = o.height;
    d.headless = o.headless;
    Context* ctx = createContext(d);
    if (!ctx) return 1;

    Renderer r;
    if (!r.init(o.shaderDir, o.width, o.height, o.headless)) return 1;

    int rc;
    if (o.bench) rc = runBench(o, *ctx, r);
    else if (o.headless) rc = runHeadless(o, *ctx, r);
    else rc = runInteractive(o, *ctx, r);
    delete ctx;
    return rc;
}
