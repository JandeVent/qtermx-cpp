// The performance smoke test: three headless workloads, end-to-end
// numbers (port of pyqtermx bench/run.py).
//
// Usage:
//     ./build/bench/run            # run all workloads, print, write baseline.json
//     ./build/bench/run --json     # machine-readable JSON, no baseline write
//
// Workloads (80×24, the reference grid), each measured end to end:
//
//   1. scroll-flood — 10k seq-style lines through feed → Snapshot
//                     (MB/s and lines/s; the input path's firehose case)
//   2. htop —         incremental color/cursor frames (10 Hz cadence);
//                     rasterize ms/frame of the damaged band + the
//                     fraction of rows each frame actually repaints
//   3. paste-burst —  1 MB bracketed paste; total elapsed ms
//
// The input seam is `Session::process` (feed → flush → emit, the reader
// thread's exact per-read step, synchronous — no thread). The paint
// seam is the renderer painting a snapshot into an offscreen QImage.
//
// Results are written to bench/results/baseline.json (env-stamped).

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPainter>

#include "renderer.h"
#include "session.h"

using namespace qtermx;
using namespace qtermx::gui;

namespace {

constexpr int kLines = 24;
constexpr int kColumns = 80;
constexpr int kChunk = 32 * 1024; // a realistic pty read size

// The bench never starts the reader thread — `Session::process` is
// driven directly. This stand-in satisfies the pty protocol for
// construction only.
class NoopPty : public PtyLike {
public:
    int masterFd() const override { return -1; }
    std::optional<std::string> read() override { return std::nullopt; }
    void sendData(const std::string&) override {}
    void setWindowSize(int, int) override {}
    void close() override {}
};

Session makeSession()
{
    static NoopPty pty;
    return Session(&pty, kLines, kColumns, 1000);
}

// Feed `data` in realistic pty-sized chunks (T6 chunking — the result
// is identical to one big feed; this is how the real thread delivers
// it).
void feed(Session& session, const std::string& data)
{
    for (size_t i = 0; i < data.size(); i += kChunk) {
        session.process(data.substr(i, std::min<size_t>(kChunk, data.size() - i)));
    }
}

double nowMs()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// 1000 app frames at 10 Hz cadence: color bands + a moving cursor row,
// each frame a small CSI payload (port of bench/frames.py).
std::vector<std::string> htopFrames(int frames = 1000)
{
    std::vector<std::string> out;
    out.reserve(frames);
    for (int f = 0; f < frames; ++f) {
        std::string frame;
        for (int band = 0; band < 5; ++band) {
            frame += "\x1b[" + std::to_string(band * 4 + 1) + ";1H\x1b[" +
                     std::to_string(31 + (band + f) % 6) + "m" + std::string(kColumns, ' ');
        }
        frame += "\x1b[" + std::to_string(f % kLines + 1) + ";1H\x1b[1mX";
        out.push_back(std::move(frame));
    }
    return out;
}

// -- workloads ---------------------------------------------------------------

struct ScrollFloodResult {
    double mbPerS;
    double linesPerS;
};

ScrollFloodResult scrollFlood(int iterations = 5)
{
    // 10k `seq`-style lines (CRLF like a pty delivers), fed through the
    // full input path: bytes → parser → screen → Snapshot emission.
    constexpr int lines = 10000;
    std::string data;
    for (int i = 1; i <= lines; ++i) {
        data += std::to_string(i) + "\r\n";
    }
    const double nbytes = static_cast<double>(data.size());

    const auto run = [&] {
        Session session = makeSession();
        session.process(""); // the initial full emit, as the thread does
        feed(session, data);
    };

    run(); // warmup
    double best = 1e18;
    for (int i = 0; i < iterations; ++i) {
        const double t0 = nowMs();
        run();
        best = std::min(best, nowMs() - t0);
    }
    const double elapsed = best / 1000.0;
    return {nbytes / elapsed / 1e6, lines / elapsed};
}

struct HtopResult {
    double rasterizeMsPerFrame;
    double damagedRowsFraction;
};

HtopResult htopIncremental(int iterations = 3)
{
    // 1000 app frames at 10 Hz cadence: color bands + a moving cursor
    // row, each frame a small CSI payload. Measures the paint path: the
    // damaged band rasterized into an offscreen QImage.
    TerminalRenderer renderer;
    QImage image(static_cast<int>(std::round(kColumns * renderer.cellW())),
                 static_cast<int>(kLines * renderer.cellH()), QImage::Format_RGB32);
    image.fill(Qt::white);

    const std::vector<std::string> frames = htopFrames();

    const auto run = [&]() -> std::pair<double, double> {
        Session session = makeSession();
        session.process(""); // initial full emit
        QPainter painter(&image);
        double paintMs = 0.0;
        int damagedRows = 0;
        for (const std::string& frame : frames) {
            session.process(frame);
            const Snapshot& snap = session.snapshots().back();
            const std::vector<int>* rowIndices = snap.full ? nullptr : &snap.dirtyRows;
            damagedRows += snap.full ? kLines : static_cast<int>(snap.dirtyRows.size());
            const double t0 = nowMs();
            renderer.paint(painter, snap, kLines, nullptr, rowIndices);
            paintMs += nowMs() - t0;
        }
        painter.end();
        return {paintMs / frames.size(), static_cast<double>(damagedRows) / frames.size() / kLines};
    };

    run(); // warmup
    double bestPaint = 1e18;
    double fraction = 0.0;
    for (int i = 0; i < iterations; ++i) {
        const auto [paintMs, frac] = run();
        if (paintMs < bestPaint) {
            bestPaint = paintMs;
            fraction = frac;
        }
    }
    return {bestPaint, fraction};
}

struct PasteResult {
    double totalMs;
    double mbPerS;
};

PasteResult pasteBurst(int iterations = 3)
{
    // 1 MB bracketed paste — the burst case where input parse
    // throughput is the wall.
    const std::string line =
        "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor ";
    std::string data = "\x1b[200~";
    for (int i = 0; i < 12400; ++i) {
        data += line + "\r\n";
    }
    data += "\x1b[201~";

    const auto run = [&] {
        Session session = makeSession();
        session.process(""); // the initial full emit, as the thread does
        feed(session, data);
    };

    run(); // warmup
    double best = 1e18;
    for (int i = 0; i < iterations; ++i) {
        const double t0 = nowMs();
        run();
        best = std::min(best, nowMs() - t0);
    }
    return {best, static_cast<double>(data.size()) / (best / 1000.0) / 1e6};
}

std::string envStamp()
{
    // The env stamp: platform + compiler + Qt version — numbers from
    // different builds are not comparable.
    std::string stamp = "darwin";
#ifdef __APPLE__
    stamp = "darwin";
#elif defined(__linux__)
    stamp = "linux";
#endif
#if defined(__clang__)
    stamp += "-clang";
#elif defined(__GNUC__)
    stamp += "-gcc";
#endif
    stamp += "-qt" QT_VERSION_STR;
    return stamp;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    const bool json = argc > 1 && std::string(argv[1]) == "--json";

    std::printf("scroll-flood  ...");
    std::fflush(stdout);
    const ScrollFloodResult flood = scrollFlood();
    std::printf(" htop  ...");
    std::fflush(stdout);
    const HtopResult htop = htopIncremental();
    std::printf(" paste-burst  ...");
    std::fflush(stdout);
    const PasteResult paste = pasteBurst();
    std::printf(" done\n");

    if (json) {
        std::printf("{\n  \"env\": {\"stamp\": \"%s\"},\n", envStamp().c_str());
        std::printf("  \"workloads\": {\n");
        std::printf("    \"scroll_flood\": {\"mb_per_s\": %.2f, \"lines_per_s\": %.2f},\n",
                    flood.mbPerS, flood.linesPerS);
        std::printf("    \"htop_incremental\": {\"rasterize_ms_per_frame\": %.3f, "
                    "\"damaged_rows_fraction\": %.3f},\n",
                    htop.rasterizeMsPerFrame, htop.damagedRowsFraction);
        std::printf("    \"paste_burst\": {\"total_ms\": %.2f, \"mb_per_s\": %.2f}\n",
                    paste.totalMs, paste.mbPerS);
        std::printf("  }\n}\n");
        return 0;
    }

    std::printf("\nscroll-flood     %8.2f MB/s   %10.2f lines/s\n", flood.mbPerS,
                flood.linesPerS);
    std::printf("htop-incremental %8.3f ms/frame rasterize  (%.1f%% of rows damaged)\n",
                htop.rasterizeMsPerFrame, htop.damagedRowsFraction * 100);
    std::printf("paste-burst      %8.2f ms total   %8.2f MB/s\n", paste.totalMs, paste.mbPerS);

    // Write the env-stamped baseline.
    const std::string path = "bench/results/baseline.json";
    std::system("mkdir -p bench/results");
    std::ofstream out(path);
    if (out) {
        out << "{\n  \"env\": {\"stamp\": \"" << envStamp() << "\"},\n";
        out << "  \"workloads\": {\n";
        out << "    \"scroll_flood\": {\"mb_per_s\": " << flood.mbPerS
            << ", \"lines_per_s\": " << flood.linesPerS << "},\n";
        out << "    \"htop_incremental\": {\"rasterize_ms_per_frame\": "
            << htop.rasterizeMsPerFrame << ", \"damaged_rows_fraction\": "
            << htop.damagedRowsFraction << "},\n";
        out << "    \"paste_burst\": {\"total_ms\": " << paste.totalMs
            << ", \"mb_per_s\": " << paste.mbPerS << "}\n";
        out << "  }\n}\n";
        std::printf("\nbaseline written to %s\n", path.c_str());
    }
    return 0;
}