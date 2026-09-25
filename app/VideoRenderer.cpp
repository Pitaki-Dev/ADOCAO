#include "VideoRenderer.hpp"
#include "GameWindow.hpp"
#include "LevelLoader.hpp"
#include "Ffmpeg.hpp"
#include "render/Planet.hpp"
#include "audio/HitsoundManager.hpp"
#include "core/timeline/Timeline.hpp"
#include "core/util/Logger.hpp"
#include "glad/gl_core.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

// Render target for headless capture: RGBA8 color texture + depth texture.
// Depth is a texture rather than a renderbuffer so the minimal glad loader
// needs no renderbuffer entry points.
class OffscreenTarget {
public:
    OffscreenTarget() = default;
    ~OffscreenTarget() { destroy(); }
    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;

    bool create(int w, int h) {
        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenTextures(1, &m_color);
        glBindTexture(GL_TEXTURE_2D, m_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_color, 0);

        glGenTextures(1, &m_depth);
        glBindTexture(GL_TEXTURE_2D, m_depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            LOG_E("Offscreen framebuffer incomplete (0x%x)", (unsigned)status);
            return false;
        }
        LOG_D("Offscreen target ready: %dx%d", w, h);
        return true;
    }

    // A fresh FBO has READ_BUFFER = COLOR_ATTACHMENT0, so glReadPixels reads the
    // color attachment without an explicit glReadBuffer call.
    void bind() const { glBindFramebuffer(GL_FRAMEBUFFER, m_fbo); }

    void destroy() {
        if (m_depth) { glDeleteTextures(1, &m_depth);   m_depth = 0; }
        if (m_color) { glDeleteTextures(1, &m_color);   m_color = 0; }
        if (m_fbo)   { glDeleteFramebuffers(1, &m_fbo); m_fbo   = 0; }
    }

private:
    GLuint m_fbo = 0, m_color = 0, m_depth = 0;
};

} // namespace

namespace {

// Rasterisers that mean "no GPU is doing the drawing".
bool isSoftwareRenderer(const std::string& name) {
    static const char* kNeedles[] = {
        "llvmpipe", "softpipe", "swrast", "software",
        "microsoft basic render", "swiftshader", "mesa offscreen",
    };
    std::string lower = name;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    for (const char* n : kNeedles)
        if (lower.find(n) != std::string::npos) return true;
    return false;
}

// Video encoding is the one part of the export that is not already on the GPU,
// so spend the GPU there: NVENC on NVIDIA, libx264 otherwise.
std::string pickEncoder(const std::string& requested, const std::string& renderer) {
    if (!requested.empty() && requested != "auto") {
        if (!ffmpegHasEncoder(requested))
            LOG_W("Encoder '%s' is not in this ffmpeg build — trying it anyway",
                  requested.c_str());
        return requested;
    }
    if (renderer.find("NVIDIA") != std::string::npos && ffmpegHasEncoder("h264_nvenc")) {
        LOG_I("NVIDIA GPU + h264_nvenc available -> encoding on the GPU");
        return "h264_nvenc";
    }
    LOG_I("No hardware encoder detected -> libx264 (CPU)");
    return "libx264";
}

} // namespace

int renderLevelToVideo(const LauncherConfig& cfg) {
    LoadResult loadResult;
    LoadingProgress progress;
    runLevelLoading(cfg, progress, loadResult);
    if (!loadResult.level || !loadResult.timeline || !loadResult.playback) {
        LOG_E("Render: failed to load level '%s'", cfg.levelPath.c_str());
        return 1;
    }

    GameWindow gw;
    if (!gw.init(cfg, loadResult)) return 1;
    if (!gw.meshReady()) {
        LOG_E("Render: track mesh not ready");
        gw.shutdown();
        return 1;
    }

    // Report what is actually drawing, and pick an encoder for it.
    const char* glVendor = (const char*)glGetString(GL_VENDOR);
    const char* glRen    = (const char*)glGetString(GL_RENDERER);
    const std::string renderer = glRen ? glRen : "?";
    LOG_I("Render GPU: %s | %s", glVendor ? glVendor : "?", renderer.c_str());
    if (isSoftwareRenderer(renderer))
        LOG_W("Software rasteriser in use (%s) — drawing will be slow. "
              "Expected on CI runners; locally, check the GPU driver is being used.",
              renderer.c_str());
    const std::string encoder = pickEncoder(cfg.videoEncoder, renderer);
    LOG_I("Video encoder: %s", encoder.c_str());

    const int w = cfg.resolutionW, h = cfg.resolutionH, fps = cfg.renderFps;
    OffscreenTarget target;
    if (!target.create(w, h)) { gw.shutdown(); return 1; }

    // Hitsounds are synthesized in memory; write them next to the output so
    // ffmpeg can mux them as a second audio track.
    std::string hitsWav;
    if (loadResult.hitsounds.isSynthesized()) {
        hitsWav = cfg.renderVideoPath + ".hitsounds.wav";
        if (!loadResult.hitsounds.writeWav(hitsWav)) hitsWav.clear();
    }

    const std::string silentVideo = cfg.renderVideoPath + ".video.mp4";
    FramePipe pipe;
    if (!pipe.open(silentVideo, w, h, fps, cfg.renderCrf, encoder)) { gw.shutdown(); return 1; }

    // t = 0 is the start of the pre-roll; the last tile is reached at
    // tileStartTimes().back(), plus a configurable tail.
    const Timeline& tl = *loadResult.timeline;
    double endSec = (double)tl.preRoll() + tl.tileStartTimes().back()
                  + (double)cfg.renderTailSeconds;
    if (cfg.renderDurationSeconds > 0.0f && (double)cfg.renderDurationSeconds < endSec)
        endSec = (double)cfg.renderDurationSeconds;
    const long long totalFrames = (long long)std::llround(endSec * (double)fps);
    LOG_I("Render: %lld frames, %dx%d @ %d fps (%.2fs)", totalFrames, w, h, fps, endSec);

    loadResult.playback->start(0.0);
    target.bind();

    std::vector<unsigned char> pixels((size_t)w * (size_t)h * 4);
    const float frameMs = 1000.0f / (float)fps;
    bool ok = true;
    for (long long f = 0; f < totalFrames && ok; ++f) {
        gw.stepOffline((double)f / (double)fps, frameMs);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        ok = pipe.writeFrame(pixels.data(), pixels.size());
        if (f % ((long long)fps * 10) == 0)
            LOG_I("  frame %lld / %lld", f, totalFrames);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!pipe.close()) ok = false;

    gw.shutdown();

    if (!ok) {
        std::remove(silentVideo.c_str());
        return 1;
    }

    // The music, and the hitsound track, both start `audioStartOffset` into the
    // pre-roll (Timeline::audioStartOffset()).
    if (!muxAudio(silentVideo, cfg.musicPath, hitsWav,
                  (double)tl.audioStartOffset(), cfg.renderVideoPath)) {
        std::remove(silentVideo.c_str());
        return 1;
    }
    std::remove(silentVideo.c_str());
    if (!hitsWav.empty()) std::remove(hitsWav.c_str());

    LOG_I("Render: wrote %s", cfg.renderVideoPath.c_str());
    return 0;
}
