#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

// Thin front-end over the ffmpeg CLI (popen). Deliberately no libav* dependency:
// ffmpeg is the wheel, we just drive it.

// Feeds raw RGBA frames into `ffmpeg -f rawvideo ... | out.mp4`.
class FramePipe {
public:
    FramePipe() = default;
    ~FramePipe();
    FramePipe(const FramePipe&) = delete;
    FramePipe& operator=(const FramePipe&) = delete;

    bool open(const std::string& outPath, int w, int h, int fps, int crf);
    bool writeFrame(const void* rgba, size_t bytes);
    bool close();  // flush + wait; false if ffmpeg exited non-zero

private:
    FILE* m_pipe = nullptr;
    std::string m_out;
};

// Muxes the silent render with the level's music and the pre-synthesized
// hitsounds. Both audio tracks are delayed by `delaySec` so they line up with
// the video timeline (the level's pre-roll/offset). Either input may be empty.
bool muxAudio(const std::string& videoIn, const std::string& musicIn,
              const std::string& hitsoundIn, double delaySec,
              const std::string& outPath);
