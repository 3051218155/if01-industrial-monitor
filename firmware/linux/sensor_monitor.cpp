/*
 * sensor_monitor - IF-01 v0.3 PL algorithm register monitor
 *
 * Registers:
 *   0x60 SENSOR_CTRL  bit0 enable, bit1 source(0=sim,1=MPU), bits3:2 fault mode
 *   0x64 SENSOR_STATUS
 *   0x68 SENSOR_RAW
 *   0x6C SENSOR_FILTERED
 *   0x70 FEATURE_RMS
 *   0x74 FEATURE_PEAK
 *   0x78 FEATURE_MEAN_ABS
 *   0x7C FEATURE_ZCR
 *   0x80 ALERT_THRESH_LOW
 *   0x84 ALERT_THRESH_HIGH
 *   0x88 MPU_WHOAMI
 */

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>

static constexpr size_t kMapSize = 0x1000;
static volatile uint32_t* g_regs = nullptr;

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

static void wr(uint32_t off, uint32_t val) {
    g_regs[off / 4] = val;
}

static int32_t sign16(uint32_t v) {
    return static_cast<int16_t>(v & 0xFFFF);
}

static void dump_one() {
    uint32_t ctrl = rd(0x60);
    uint32_t st   = rd(0x64);
    std::printf("CTRL=0x%08X STATUS=0x%08X RAW=%d FILTERED=%d "
                "RMS=%u PEAK=%u MEAN_ABS=%u ZCR=%u WHOAMI=0x%02X\n",
                ctrl, st, sign16(rd(0x68)), sign16(rd(0x6C)),
                rd(0x70), rd(0x74) & 0xFFFF, rd(0x78), rd(0x7C) & 0xFFFF,
                rd(0x88) & 0xFF);
}

static int stream(int seconds, int period_ms, const std::string& csv_path) {
    std::ofstream csv;
    if (!csv_path.empty()) {
        csv.open(csv_path, std::ios::app);
        if (csv.is_open()) {
            csv.seekp(0, std::ios::end);
            if (csv.tellp() == 0)
                csv << "timestamp_ms,status,raw,filtered,rms,peak,mean_abs,zcr,whoami\n";
        } else {
            std::fprintf(stderr, "warn: cannot open %s\n", csv_path.c_str());
        }
    }

    auto begin = std::chrono::steady_clock::now();
    int count = 0;
    while (seconds <= 0 || std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - begin)
                              .count() < seconds) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      now.time_since_epoch())
                      .count();
        uint32_t st = rd(0x64);
        int32_t raw = sign16(rd(0x68));
        int32_t filtered = sign16(rd(0x6C));
        uint32_t rms = rd(0x70);
        uint32_t peak = rd(0x74) & 0xFFFF;
        uint32_t mean_abs = rd(0x78);
        uint32_t zcr = rd(0x7C) & 0xFFFF;
        uint32_t whoami = rd(0x88) & 0xFF;

        std::printf("[%04d] raw=%d filtered=%d rms=%u peak=%u mean_abs=%u zcr=%u "
                    "status=0x%08X whoami=0x%02X\n",
                    count++, raw, filtered, rms, peak, mean_abs, zcr, st, whoami);
        std::fflush(stdout);

        if (csv.is_open()) {
            csv << ms << ',' << st << ',' << raw << ',' << filtered << ','
                << rms << ',' << peak << ',' << mean_abs << ',' << zcr << ','
                << whoami << '\n';
            csv.flush();
        }
        usleep(period_ms * 1000);
    }
    return 0;
}

static void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage:\n"
                 "  %s show\n"
                 "  %s stream <seconds> <period_ms> [csv]\n"
                 "  %s enable <0|1>\n"
                 "  %s source sim|mpu\n"
                 "  %s fault <0..3>\n"
                 "  %s threshold <low> <high>\n"
                 "  %s set <ctrl_hex>\n"
                 "\n"
                 "PL_REG_BASE default 0x80000000\n",
                 prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char* env = std::getenv("PL_REG_BASE");
    uint32_t base = env ? parse_u32(env, "PL_REG_BASE") : 0x80000000u;
    g_regs = map_pl_base(base);

    if (!std::strcmp(argv[1], "show")) {
        dump_one();
        return 0;
    }
    if (!std::strcmp(argv[1], "stream")) {
        if (argc < 4) {
            usage(argv[0]);
            return 1;
        }
        std::string csv = argc >= 5 ? argv[4] : "/root/sensor_stream.csv";
        return stream(parse_u32(argv[2], "seconds"),
                      parse_u32(argv[3], "period_ms"), csv);
    }
    if (!std::strcmp(argv[1], "enable")) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        uint32_t ctrl = rd(0x60);
        ctrl = parse_u32(argv[2], "enable") ? (ctrl | 1u) : (ctrl & ~1u);
        wr(0x60, ctrl & 0xF);
        dump_one();
        return 0;
    }
    if (!std::strcmp(argv[1], "source")) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        uint32_t ctrl = rd(0x60);
        if (!std::strcmp(argv[2], "mpu"))
            ctrl |= 2u;
        else
            ctrl &= ~2u;
        wr(0x60, ctrl & 0xF);
        dump_one();
        return 0;
    }
    if (!std::strcmp(argv[1], "fault")) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        uint32_t mode = parse_u32(argv[2], "fault") & 3u;
        uint32_t ctrl = rd(0x60);
        ctrl = (ctrl & ~(3u << 2)) | (mode << 2);
        wr(0x60, ctrl & 0xF);
        dump_one();
        return 0;
    }
    if (!std::strcmp(argv[1], "threshold")) {
        if (argc < 4) {
            usage(argv[0]);
            return 1;
        }
        wr(0x80, parse_u32(argv[2], "low"));
        wr(0x84, parse_u32(argv[3], "high"));
        dump_one();
        return 0;
    }
    if (!std::strcmp(argv[1], "set")) {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        wr(0x60, parse_u32(argv[2], "ctrl") & 0xF);
        dump_one();
        return 0;
    }

    usage(argv[0]);
    return 1;
}
