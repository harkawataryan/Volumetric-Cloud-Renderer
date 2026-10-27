#include "renderer.h"
#include "gl.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace {

const int SHAPE_RES = 128;
const int DETAIL_RES = 32;
const int WEATHER_RES = 512;

void texParams(GLenum target, GLenum wrap, GLenum filter) {
    glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
    if (target == GL_TEXTURE_3D) glTexParameteri(target, GL_TEXTURE_WRAP_R, wrap);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
}

unsigned makeTexture2D(GLenum format, int w, int h, GLenum filter) {
    unsigned t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexStorage2D(GL_TEXTURE_2D, 1, format, w, h);
    texParams(GL_TEXTURE_2D, GL_CLAMP_TO_EDGE, filter);
    return t;
}

void bindTex(unsigned unit, GLenum target, unsigned tex, unsigned prog, const char* name) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target, tex);
    glUniform1i(glGetUniformLocation(prog, name), (int)unit);
}

void setMat(unsigned prog, const char* name, const Mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, m.m);
}

}  // namespace

std::string Renderer::load(const std::string& file) {
    std::ifstream in(shaderDir_ + "/" + file);
    if (!in) {
        fprintf(stderr, "cannot open shader %s/%s\n", shaderDir_.c_str(), file.c_str());
        return "";
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

unsigned Renderer::compile(unsigned type, const std::string& body, const char* name) {
    static std::string common;
    if (common.empty()) common = load("common.glsl");
    std::string src = "#version 450 core\n" + common + "\n#line 1\n" + body;
    unsigned s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "%s: compile error:\n%s\n", name, log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned Renderer::link(const std::vector<unsigned>& shaders, const char* name) {
    for (unsigned s : shaders) if (!s) return 0;
    unsigned p = glCreateProgram();
    for (unsigned s : shaders) glAttachShader(p, s);
    glLinkProgram(p);
    for (unsigned s : shaders) glDeleteShader(s);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, nullptr, log);
        fprintf(stderr, "%s: link error:\n%s\n", name, log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool Renderer::buildPrograms() {
    Program ns{link({compile(GL_COMPUTE_SHADER, load("noise_shape.comp"), "noise_shape")}, "noise_shape")};
    Program nd{link({compile(GL_COMPUTE_SHADER, load("noise_detail.comp"), "noise_detail")}, "noise_detail")};
    Program we{link({compile(GL_COMPUTE_SHADER, load("weather.comp"), "weather")}, "weather")};
    std::string vs = load("fullscreen.vert");
    Program cl{link({compile(GL_VERTEX_SHADER, vs, "fullscreen"), compile(GL_FRAGMENT_SHADER, load("clouds.frag"), "clouds")}, "clouds")};
    Program re{link({compile(GL_VERTEX_SHADER, vs, "fullscreen"), compile(GL_FRAGMENT_SHADER, load("resolve.frag"), "resolve")}, "resolve")};
    Program co{link({compile(GL_VERTEX_SHADER, vs, "fullscreen"), compile(GL_FRAGMENT_SHADER, load("composite.frag"), "composite")}, "composite")};
    if (!ns.id || !nd.id || !we.id || !cl.id || !re.id || !co.id) return false;
    for (Program* p : {&noiseShape_, &noiseDetail_, &weatherProg_, &cloudProg_, &resolveProg_, &compositeProg_})
        if (p->id) glDeleteProgram(p->id);
    noiseShape_ = ns; noiseDetail_ = nd; weatherProg_ = we;
    cloudProg_ = cl; resolveProg_ = re; compositeProg_ = co;
    return true;
}

bool Renderer::reloadShaders() {
    if (!buildPrograms()) return false;
    generateNoise();
    historyValid_ = false;
    return true;
}

void Renderer::generateNoise() {
    if (!shapeTex_) {
        glGenTextures(1, &shapeTex_);
        glBindTexture(GL_TEXTURE_3D, shapeTex_);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, SHAPE_RES, SHAPE_RES, SHAPE_RES);
        texParams(GL_TEXTURE_3D, GL_REPEAT, GL_LINEAR);
        glGenTextures(1, &detailTex_);
        glBindTexture(GL_TEXTURE_3D, detailTex_);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, DETAIL_RES, DETAIL_RES, DETAIL_RES);
        texParams(GL_TEXTURE_3D, GL_REPEAT, GL_LINEAR);
        glGenTextures(1, &weatherTex_);
        glBindTexture(GL_TEXTURE_2D, weatherTex_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, WEATHER_RES, WEATHER_RES);
        texParams(GL_TEXTURE_2D, GL_REPEAT, GL_LINEAR);
    }
    glUseProgram(noiseShape_.id);
    glBindImageTexture(0, shapeTex_, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(SHAPE_RES / 8, SHAPE_RES / 8, SHAPE_RES / 8);
    glUseProgram(noiseDetail_.id);
    glBindImageTexture(0, detailTex_, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(DETAIL_RES / 8, DETAIL_RES / 8, DETAIL_RES / 8);
    glUseProgram(weatherProg_.id);
    glUniform1f(glGetUniformLocation(weatherProg_.id, "uSeed"), 0.0f);
    glBindImageTexture(0, weatherTex_, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
    glDispatchCompute(WEATHER_RES / 16, WEATHER_RES / 16, 1);
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Renderer::makeTargets() {
    unsigned old[] = {cloudColor_, cloudDepth_, resolveColor_[0], resolveColor_[1]};
    glDeleteTextures(4, old);
    unsigned oldFbo[] = {cloudFbo_, resolveFbo_[0], resolveFbo_[1]};
    glDeleteFramebuffers(3, oldFbo);

    cloudColor_ = makeTexture2D(GL_RGBA16F, cloudW_, cloudH_, GL_LINEAR);
    cloudDepth_ = makeTexture2D(GL_R32F, cloudW_, cloudH_, GL_LINEAR);
    glGenFramebuffers(1, &cloudFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, cloudFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cloudColor_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, cloudDepth_, 0);
    const GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, bufs);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "cloud framebuffer incomplete\n");

    for (int i = 0; i < 2; ++i) {
        resolveColor_[i] = makeTexture2D(GL_RGBA16F, width_, height_, GL_LINEAR);
        glGenFramebuffers(1, &resolveFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveColor_[i], 0);
        glDrawBuffers(1, bufs);
    }
    if (offscreen_) {
        glDeleteTextures(1, &outputTex_);
        glDeleteFramebuffers(1, &outputFbo_);
        outputTex_ = makeTexture2D(GL_RGBA8, width_, height_, GL_NEAREST);
        glGenFramebuffers(1, &outputFbo_);
        glBindFramebuffer(GL_FRAMEBUFFER, outputFbo_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex_, 0);
        glDrawBuffers(1, bufs);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    historyValid_ = false;
}

bool Renderer::init(const std::string& shaderDir, int width, int height, bool offscreen) {
    shaderDir_ = shaderDir;
    offscreen_ = offscreen;
    fprintf(stderr, "GL %s on %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenQueries(4, queries_);
    if (!buildPrograms()) return false;
    generateNoise();
    resize(width, height);
    return true;
}

void Renderer::resize(int width, int height) {
    width_ = width;
    height_ = height;
    lastScale_ = 0.f;  // forces target rebuild on next render
}

void Renderer::render(const Camera& cam, const Weather& w, const RenderSettings& s, float timeSec) {
    if (s.scale != lastScale_) {
        lastScale_ = s.scale;
        cloudW_ = std::max(1, (int)(width_ * s.scale));
        cloudH_ = std::max(1, (int)(height_ * s.scale));
        makeTargets();
    }
    float aspect = (float)width_ / (float)height_;
    Mat4 view = lookAt(cam.pos, cam.forward(), Vec3(0, 1, 0));
    Mat4 proj = perspective(cam.fovY, aspect, 1.0f, 100000.0f);
    Mat4 viewProj = mul(proj, view);
    float jx = 0.f, jy = 0.f;
    if (s.temporal) {
        jx = (halton(frame_ % 16 + 1, 2) - 0.5f) * 2.0f / (float)cloudW_;
        jy = (halton(frame_ % 16 + 1, 3) - 0.5f) * 2.0f / (float)cloudH_;
    }
    Mat4 jitteredVP = mul(jitterProjection(proj, jx, jy), view);
    Mat4 invJitteredVP = inverse(jitteredVP);
    Mat4 invVP = inverse(viewProj);
    Vec3 sun = normalize(Vec3(std::cos(w.sunElevation) * std::cos(w.sunAzimuth), std::sin(w.sunElevation),
                              std::cos(w.sunElevation) * std::sin(w.sunAzimuth)));

    glQueryCounter(queries_[0], GL_TIMESTAMP);

    // 1. Cloud pass at reduced resolution.
    glBindFramebuffer(GL_FRAMEBUFFER, cloudFbo_);
    glViewport(0, 0, cloudW_, cloudH_);
    unsigned p = cloudProg_.id;
    glUseProgram(p);
    bindTex(0, GL_TEXTURE_3D, shapeTex_, p, "uShape");
    bindTex(1, GL_TEXTURE_3D, detailTex_, p, "uDetail");
    bindTex(2, GL_TEXTURE_2D, weatherTex_, p, "uWeather");
    setMat(p, "uInvViewProj", invJitteredVP);
    glUniform3f(glGetUniformLocation(p, "uCamPos"), cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(glGetUniformLocation(p, "uSunDir"), sun.x, sun.y, sun.z);
    glUniform1f(glGetUniformLocation(p, "uTime"), timeSec);
    glUniform1i(glGetUniformLocation(p, "uFrame"), (int)frame_);
    glUniform1i(glGetUniformLocation(p, "uMaxSteps"), s.maxSteps);
    glUniform1f(glGetUniformLocation(p, "uCoverage"), w.coverage);
    glUniform1f(glGetUniformLocation(p, "uDensity"), w.density);
    glUniform1f(glGetUniformLocation(p, "uCloudType"), w.cloudType);
    glUniform1f(glGetUniformLocation(p, "uBaseHeight"), w.baseHeight);
    glUniform1f(glGetUniformLocation(p, "uThickness"), w.thickness);
    glUniform2f(glGetUniformLocation(p, "uWind"), w.windX, w.windZ);
    glUniform1f(glGetUniformLocation(p, "uAnisotropy"), w.anisotropy);
    glUniform1f(glGetUniformLocation(p, "uSunIntensity"), w.sunIntensity);
    glUniform1f(glGetUniformLocation(p, "uExtinction"), w.extinction);
    glUniform1f(glGetUniformLocation(p, "uAmbient"), w.ambient);
    glUniform1f(glGetUniformLocation(p, "uErosion"), w.erosion);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glQueryCounter(queries_[1], GL_TIMESTAMP);

    // 2. Temporal resolve into the other history buffer.
    int write = historyIndex_ ^ 1;
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_[write]);
    glViewport(0, 0, width_, height_);
    p = resolveProg_.id;
    glUseProgram(p);
    bindTex(0, GL_TEXTURE_2D, cloudColor_, p, "uCurrent");
    bindTex(1, GL_TEXTURE_2D, cloudDepth_, p, "uCurrentDepth");
    bindTex(2, GL_TEXTURE_2D, resolveColor_[historyIndex_], p, "uHistory");
    setMat(p, "uInvViewProj", invVP);
    setMat(p, "uPrevViewProj", prevViewProj_);
    glUniform3f(glGetUniformLocation(p, "uCamPos"), cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform1i(glGetUniformLocation(p, "uHistoryValid"), (s.temporal && historyValid_) ? 1 : 0);
    glUniform1f(glGetUniformLocation(p, "uBlend"), 0.08f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glQueryCounter(queries_[2], GL_TIMESTAMP);

    // 3. Composite with sky and ground to the output.
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo_);
    glViewport(0, 0, width_, height_);
    p = compositeProg_.id;
    glUseProgram(p);
    bindTex(0, GL_TEXTURE_2D, resolveColor_[write], p, "uClouds");
    bindTex(1, GL_TEXTURE_2D, cloudDepth_, p, "uCloudDepth");
    bindTex(2, GL_TEXTURE_3D, shapeTex_, p, "uShape");
    setMat(p, "uInvViewProj", invVP);
    glUniform3f(glGetUniformLocation(p, "uCamPos"), cam.pos.x, cam.pos.y, cam.pos.z);
    glUniform3f(glGetUniformLocation(p, "uSunDir"), sun.x, sun.y, sun.z);
    glUniform1i(glGetUniformLocation(p, "uShowNoise"), s.showNoise ? 1 : 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glQueryCounter(queries_[3], GL_TIMESTAMP);

    historyIndex_ = write;
    historyValid_ = s.temporal;
    prevViewProj_ = viewProj;
    frame_++;

    GLuint64 ts[4];
    for (int i = 0; i < 4; ++i) glGetQueryObjectui64v(queries_[i], GL_QUERY_RESULT, &ts[i]);
    lastTimings_.cloudMs = (ts[1] - ts[0]) * 1e-6;
    lastTimings_.resolveMs = (ts[2] - ts[1]) * 1e-6;
    lastTimings_.compositeMs = (ts[3] - ts[2]) * 1e-6;
    lastTimings_.totalMs = (ts[3] - ts[0]) * 1e-6;
}

std::vector<unsigned char> Renderer::readback(int& w, int& h) {
    w = width_;
    h = height_;
    std::vector<unsigned char> px((size_t)w * h * 3), out(px.size());
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; ++y)
        std::copy(px.begin() + (size_t)y * w * 3, px.begin() + (size_t)(y + 1) * w * 3,
                  out.begin() + (size_t)(h - 1 - y) * w * 3);
    return out;
}
