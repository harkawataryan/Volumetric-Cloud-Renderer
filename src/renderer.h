#pragma once
#include "math.h"
#include <string>
#include <vector>

struct Weather {
    float coverage = 0.5f;    // 0..1, shifts the weather map's coverage
    float density = 1.0f;     // extinction multiplier
    float cloudType = 0.55f;  // 0 stratus .. 1 cumulonimbus
    float baseHeight = 1500.f;
    float thickness = 2500.f;
    float windX = 12.f, windZ = 4.f;
    float sunAzimuth = 0.9f;   // radians
    float sunElevation = 0.4f;
    float anisotropy = 0.6f;   // forward HG lobe g
    float sunIntensity = 10.f;
    float extinction = 0.012f; // per metre at density 1
    float ambient = 1.0f;
    float erosion = 0.45f;     // how much the detail noise eats into cloud edges
};

struct RenderSettings {
    float scale = 0.5f;        // cloud pass resolution relative to output
    bool temporal = true;      // reprojection + accumulation
    int maxSteps = 128;
    bool showNoise = false;
};

struct Camera {
    Vec3 pos{0, 400, 0};
    float yaw = 0.3f, pitch = 0.4f;
    float fovY = 60.f * 3.14159265f / 180.f;
    Vec3 forward() const {
        return normalize(Vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), -std::cos(pitch) * std::cos(yaw)));
    }
};

struct FrameTimings {
    double cloudMs = 0, resolveMs = 0, compositeMs = 0, totalMs = 0;
};

class Renderer {
public:
    // offscreen=true renders the final image into an internal framebuffer
    // (needed under surfaceless EGL, which has no default framebuffer).
    bool init(const std::string& shaderDir, int width, int height, bool offscreen);
    void resize(int width, int height);
    // Renders one frame into the default framebuffer (or fbo 0 for headless).
    void render(const Camera& cam, const Weather& w, const RenderSettings& s, float timeSec);
    // Reads the last frame back as RGB8, bottom-up rows flipped to top-down.
    std::vector<unsigned char> readback(int& w, int& h);
    FrameTimings timings() const { return lastTimings_; }
    void resetHistory() { historyValid_ = false; }
    bool reloadShaders();

private:
    struct Program { unsigned id = 0; };
    bool buildPrograms();
    unsigned compile(unsigned type, const std::string& src, const char* name);
    unsigned link(const std::vector<unsigned>& shaders, const char* name);
    std::string load(const std::string& file);
    void makeTargets();
    void generateNoise();

    std::string shaderDir_;
    int width_ = 0, height_ = 0, cloudW_ = 0, cloudH_ = 0;
    float lastScale_ = 0.f;
    unsigned vao_ = 0;
    Program noiseShape_, noiseDetail_, weatherProg_, cloudProg_, resolveProg_, compositeProg_;
    unsigned shapeTex_ = 0, detailTex_ = 0, weatherTex_ = 0;
    unsigned cloudFbo_ = 0, cloudColor_ = 0, cloudDepth_ = 0;
    unsigned resolveFbo_[2] = {0, 0}, resolveColor_[2] = {0, 0}, resolveDepth_[2] = {0, 0};
    unsigned outputFbo_ = 0, outputTex_ = 0;
    bool offscreen_ = false;
    int historyIndex_ = 0;
    bool historyValid_ = false;
    Mat4 prevViewProj_;
    unsigned frame_ = 0;
    unsigned queries_[4] = {0, 0, 0, 0};
    FrameTimings lastTimings_;
};
