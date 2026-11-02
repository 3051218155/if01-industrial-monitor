/*
 * fault_ai - board-side quantized fault classifier.
 *
 * The current IF-01 bitstream does not include the vendor Soft NPU IP, so this
 * program is the CPU fallback. The same model/input features are intended to
 * be moved to the NPU backend after the merged NPU+IF-01 bitstream is ready.
 */

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "fault_model_weights.h"

static constexpr size_t kMapSize = 0x1000;
static constexpr int32_t kQ = 4096;

static volatile uint32_t* g_regs = nullptr;

struct Features {
    uint32_t rms;
    uint32_t peak;
    uint32_t mean_abs;
    uint32_t zcr;
};

struct Result {
    int label;
    float confidence;
    float probs[3];
};

static uint32_t parse_u32(const char* s, const char* what) {
    char* end = nullptr;
    errno = 0;
    unsigned long v = std::strtoul(s, &end, 0);
    if (errno || end == s || *end || v > 0xFFFFFFFFUL) {
        std::fprintf(stderr, "invalid %s: %s\n", what, s);
        std::exit(1);
    }
    return static_cast<uint32_t>(v);
}

static volatile uint32_t* map_pl_base(uint32_t base_phys) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        std::perror("open /dev/mem (need root)");
        std::exit(1);
    }
    void* p = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   static_cast<off_t>(base_phys));
    close(fd);
    if (p == MAP_FAILED) {
        std::perror("mmap");
        std::exit(1);
    }
    return static_cast<volatile uint32_t*>(p);
}

static uint32_t rd(uint32_t off) {
    return g_regs[off / 4];
}

static Features read_features() {
    return {
        rd(0x70),
        rd(0x74) & 0xFFFF,
        rd(0x78),
        rd(0x7C) & 0xFFFF,
    };
}

static int32_t normalize_q12(uint32_t value, int index) {
    int64_t v = static_cast<int64_t>(value) - kFeatureMin[index];
    int64_t span = kFeatureMax[index] - kFeatureMin[index];
    if (v < 0)
        v = 0;
    if (v > span)
        v = span;
    return static_cast<int32_t>((v * kQ) / span);
}

static int32_t dot_relu(const int32_t* input, const int16_t* weights, int in_count,
                        int32_t bias) {
    int64_t acc = bias;
    for (int i = 0; i < in_count; ++i)
        acc += static_cast<int64_t>(input[i]) * weights[i];
    acc >>= 12;
    return acc > 0 ? static_cast<int32_t>(acc) : 0;
}

static Result infer(const Features& f) {
    int32_t input[4] = {
        normalize_q12(f.rms, 0),
        normalize_q12(f.peak, 1),
        normalize_q12(f.mean_abs, 2),
        normalize_q12(f.zcr, 3),
    };

    int32_t h0[12];
    for (int j = 0; j < 12; ++j) {
        int64_t acc = B0[j];
        for (int i = 0; i < 4; ++i)
            acc += static_cast<int64_t>(input[i]) * W0[i][j];
        acc >>= 12;
        h0[j] = acc > 0 ? static_cast<int32_t>(acc) : 0;
    }

    int32_t h1[6];
    for (int j = 0; j < 6; ++j)
        h1[j] = dot_relu(h0, &W1[0][j], 12, B1[j]);

    int32_t logits[3];
    for (int j = 0; j < 3; ++j) {
        int64_t acc = B2[j];
        for (int i = 0; i < 6; ++i)
            acc += static_cast<int64_t>(h1[i]) * W2[i][j];
        acc >>= 12;
        logits[j] = static_cast<int32_t>(acc);
    }

    int32_t max_logit = std::max(logits[0], std::max(logits[1], logits[2]));
    float sum = 0.0f;
    Result result{};
    for (int i = 0; i < 3; ++i) {
        result.probs[i] = std::exp(static_cast<float>(logits[i] - max_logit) / kQ);
        sum += result.probs[i];
    }
    result.label = 0;
    for (int i = 0; i < 3; ++i) {
        result.probs[i] /= sum;
        if (result.probs[i] > result.probs[result.label])
            result.label = i;
    }
    result.confidence = result.probs[result.label];
    return result;
}

static const char* label_name(int label) {
    static const char* names[] = {"normal", "imbalance", "looseness"};
    return label >= 0 && label < 3 ? names[label] : "unknown";
}

static void print_result(const Features& f, const Result& r) {
    std::printf(
        "rms=%u peak=%u mean_abs=%u zcr=%u class=%s confidence=%.3f "
        "probs=%.3f,%.3f,%.3f\n",
        f.rms, f.peak, f.mean_abs, f.zcr, label_name(r.label), r.confidence,
        r.probs[0], r.probs[1], r.probs[2]);
}

static void cmd_bench(int iterations) {
    Features f = read_features();
    Result r{};
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
        r = infer(f);
    const auto end = std::chrono::steady_clock::now();
    const double total_us =
        std::chrono::duration<double, std::micro>(end - begin).count();
    print_result(f, r);
    std::printf("iterations=%d total_us=%.3f avg_us=%.3f\n", iterations, total_us,
                total_us / iterations);
}

static void cmd_stream(int seconds, int period_ms, const std::string& csv_path) {
    std::ofstream csv(csv_path, std::ios::app);
    if (csv.is_open()) {
        csv.seekp(0, std::ios::end);
        if (csv.tellp() == 0)
            csv << "timestamp_ms,rms,peak,mean_abs,zcr,class,confidence,"
                   "prob_normal,prob_imbalance,prob_looseness\n";
    }

    const auto begin = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - begin)
               .count() < seconds) {
        Features f = read_features();
        Result r = infer(f);
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count();
        print_result(f, r);
        if (csv.is_open()) {
            csv << ms << ',' << f.rms << ',' << f.peak << ',' << f.mean_abs << ','
                << f.zcr << ',' << r.label << ',' << r.confidence << ','
                << r.probs[0] << ',' << r.probs[1] << ',' << r.probs[2] << '\n';
            csv.flush();
        }
        usleep(period_ms * 1000);
    }
}

static void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s show\n"
                 "  %s bench [iterations]\n"
                 "  %s stream <seconds> <period_ms> [csv]\n"
                 "PL_REG_BASE default 0x80000000\n",
                 prog, prog, prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* env = std::getenv("PL_REG_BASE");
    const uint32_t base = env ? parse_u32(env, "PL_REG_BASE") : 0x80000000u;
    g_regs = map_pl_base(base);

    if (std::strcmp(argv[1], "show") == 0) {
        Features f = read_features();
        print_result(f, infer(f));
        return 0;
    }
    if (std::strcmp(argv[1], "bench") == 0) {
        int iterations = argc >= 3 ? static_cast<int>(parse_u32(argv[2], "iterations"))
                                   : 10000;
        cmd_bench(std::max(iterations, 1));
        return 0;
    }
    if (std::strcmp(argv[1], "stream") == 0) {
        if (argc < 4) {
            usage(argv[0]);
            return 1;
        }
        int seconds = static_cast<int>(parse_u32(argv[2], "seconds"));
        int period_ms = static_cast<int>(parse_u32(argv[3], "period_ms"));
        std::string csv = argc >= 5 ? argv[4] : "/root/fault_ai.csv";
        cmd_stream(std::max(seconds, 1), std::max(period_ms, 1), csv);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
