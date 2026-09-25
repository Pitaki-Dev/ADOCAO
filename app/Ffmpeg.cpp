#include "Ffmpeg.hpp"
#include "core/util/Logger.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

// Single-quote a path so it survives /bin/sh -c. On Windows the shell is
// cmd.exe, which has no single-quote syntax — use double quotes there.
std::string quote(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string q = "'";
    for (char c : s) {
        if (c == '\'') q += "'\\''";
        else           q += c;
    }
    return q + "'";
#endif
}

std::string delayChain(int inputIndex, int delayMs) {
    return "[" + std::to_string(inputIndex) + ":a]"
         + "adelay=" + std::to_string(delayMs) + ":all=1[a" + std::to_string(inputIndex) + "];";
}

// Quality flags differ per encoder family. `crf` means "constant quality" for
// all of them, but NVENC spells it `-cq` and needs the bitrate target zeroed.
std::string encoderArgs(const std::string& enc, int crf) {
    const std::string q = std::to_string(crf);
    if (enc == "h264_nvenc" || enc == "hevc_nvenc" || enc == "av1_nvenc")
        return "-c:v " + enc + " -preset p5 -tune hq -rc vbr -cq " + q + " -b:v 0";
    if (enc == "h264_qsv" || enc == "hevc_qsv")
        return "-c:v " + enc + " -global_quality " + q;
    if (enc == "h264_amf" || enc == "hevc_amf")
        return "-c:v " + enc + " -quality quality -rc cqp -qp_i " + q + " -qp_p " + q;
    if (enc == "libx264" || enc == "libx265")
        return "-c:v " + enc + " -preset medium -crf " + q;
    return "-c:v " + enc + " -crf " + q;
}

bool tokenPresent(const std::string& haystack, const std::string& needle) {
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        const bool leftOk  = pos == 0 || isspace((unsigned char)haystack[pos - 1]);
        const size_t end   = pos + needle.size();
        const bool rightOk = end >= haystack.size() || isspace((unsigned char)haystack[end]);
        if (leftOk && rightOk) return true;
        pos = end;
    }
    return false;
}

} // namespace

bool ffmpegHasEncoder(const std::string& name) {
    FILE* p = popen("ffmpeg -hide_banner -loglevel error -encoders", "r");
    if (!p) return false;
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
    pclose(p);
    return tokenPresent(out, name);
}

FramePipe::~FramePipe() {
    if (m_pipe) pclose(m_pipe);
}

bool FramePipe::open(const std::string& outPath, int w, int h, int fps, int crf,
                     const std::string& encoder) {
    m_out = outPath;
    std::string cmd =
        "ffmpeg -y -hide_banner -loglevel error -nostdin "
        "-f rawvideo -pixel_format rgba -video_size " + std::to_string(w) + "x" + std::to_string(h) +
        " -framerate " + std::to_string(fps) + " -i - "
        "-vf vflip " + encoderArgs(encoder, crf) +
        " -pix_fmt yuv420p " + quote(outPath);
    m_pipe = popen(cmd.c_str(), "w");
    if (!m_pipe) {
        LOG_E("Failed to spawn ffmpeg: %s", cmd.c_str());
        return false;
    }
#ifdef _WIN32
    // _popen hands back a text-mode pipe. Without this, every 0x0A byte in the
    // raw pixel stream expands to 0x0D 0x0A and the video comes out corrupt.
    _setmode(_fileno(m_pipe), _O_BINARY);
#endif
    LOG_D("FramePipe: %s", cmd.c_str());
    return true;
}

bool FramePipe::writeFrame(const void* rgba, size_t bytes) {
    if (!m_pipe) return false;
    if (fwrite(rgba, 1, bytes, m_pipe) == bytes) return true;
    LOG_E("FramePipe: write failed (ffmpeg died?)");
    return false;
}

bool FramePipe::close() {
    if (!m_pipe) return false;
    int rc = pclose(m_pipe);
    m_pipe = nullptr;
    if (rc != 0) {
        LOG_E("ffmpeg failed with status %d while writing %s", rc, m_out.c_str());
        return false;
    }
    return true;
}

bool muxAudio(const std::string& videoIn, const std::string& musicIn,
              const std::string& hitsoundIn, double delaySec,
              const std::string& outPath) {
    if (musicIn.empty() && hitsoundIn.empty()) {
        LOG_W("muxAudio: no audio inputs, keeping silent video");
        return true;
    }

    const int delayMs = (int)std::llround(delaySec < 0.0 ? 0.0 : delaySec * 1000.0);
    std::string cmd = "ffmpeg -y -hide_banner -loglevel error -nostdin -i " + quote(videoIn);

    std::vector<std::string> labels;
    std::string filter;
    int idx = 1;
    for (const std::string* in : {&musicIn, &hitsoundIn}) {
        if (in->empty()) continue;
        cmd += " -i " + quote(*in);
        filter += delayChain(idx, delayMs);
        labels.push_back("[a" + std::to_string(idx) + "]");
        idx++;
    }

    // apad keeps audio from ending the output early; -shortest then trims to
    // the video length. normalize=0 preserves the original track levels.
    std::string chain = filter;
    for (const auto& l : labels) chain += l;
    if (labels.size() > 1)
        chain += "amix=inputs=" + std::to_string(labels.size()) + ":normalize=0,";
    chain += "apad[a]";

    cmd += " -filter_complex " + quote(chain) +
           " -map 0:v -map \"[a]\" -c:v copy -c:a aac -b:a 192k -shortest "
           "-movflags +faststart " + quote(outPath);

    LOG_D("muxAudio: %s", cmd.c_str());
    int rc = system(cmd.c_str());
    if (rc != 0) {
        LOG_E("ffmpeg mux failed with status %d", rc);
        return false;
    }
    return true;
}
