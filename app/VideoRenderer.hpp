#pragma once

#include "LauncherWindow.hpp"

// Offline video rendering (--render <file>).
//
// Plays the level on a deterministic clock — frame N is always t = N / fps, so
// the output does not depend on how fast the machine can draw. Frames are read
// back from an offscreen FBO and piped to ffmpeg; the level's music and
// hitsounds are muxed in afterwards.
//
// Requires glfwInit() to have been called (a GL context is still needed, so a
// display — real or Xvfb — must be available). Returns 0 on success.
int renderLevelToVideo(const LauncherConfig& cfg);
