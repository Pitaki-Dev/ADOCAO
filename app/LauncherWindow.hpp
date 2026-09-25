#pragma once

#include <string>
#include <memory>
#include <functional>

class LevelData;
class Timeline;

// User selections from the launcher
struct LauncherConfig {
    std::string levelPath;
    std::string musicPath;
    std::string trackFillColor   = "DEBB7B";  // 6-char hex
    std::string trackStrokeColor = "6F5D3D";  // 6-char hex (DEBB7B * 0.5)
    std::string backgroundColor  = "000000";  // 6-char hex
    bool   autoStroke   = true;
    bool   enableHitsounds = true;
    std::string forceHitsoundType;  // force "None" hitsound to this type (empty = disabled)
    bool   autoPlay = false;       // auto-start playback after loading
    bool   legacyCulling = false;  // use legacy brute-force culling
    int    msaaSamples = 0;        // MSAA samples (0=off, 2, 4, 8)
    bool   exclusiveFullscreen = true; // exclusive fullscreen (vs borderless windowed)
    int  resolutionW = 1920;
    int  resolutionH = 1080;
    bool fullscreen = false;
    bool showTrail = true;
    float trailDuration = 0.4f;    // seconds of trail history
    float trailSampleRate = 200.0f; // samples per second
    bool exportHitsounds = false;
    std::string exportDir;         // hitsound export directory (defaults to level dir)
    bool cancelled = false;

    // Offline video rendering (--render <file>). A non-empty path switches the
    // app into headless render mode: hidden window, deterministic frame clock,
    // no audio device; frames are piped to ffmpeg and the music + hitsounds are
    // muxed in afterwards. See app/VideoRenderer.cpp.
    std::string renderVideoPath;
    int   renderFps         = 60;
    int   renderCrf         = 18;
    float renderTailSeconds = 1.0f;
    float cameraZoom        = 0.0f;   // --zoom: overrides the level's settings.zoom (0 = keep level value)
    float renderDurationSeconds = 0.0f; // --duration: cap the render length (0 = whole level)
    bool offlineRender() const { return !renderVideoPath.empty(); }

    // Wizard (5.0.0): result of the "Next" preload step (parse + timeline).
    // Hitsound synthesis + audio are finished after Start in runLevelLoading.
    std::shared_ptr<LevelData> preloadedLevel;
    std::shared_ptr<Timeline> preloadedTimeline;
};

// Opens a centered ImGui launcher window. Returns config after user clicks Start or closes.
LauncherConfig showLauncher();
