/*
 * perf_bench - board-side quantitative tests for IF-01 phase 2.
 *
 * controls <count>       Safe motor-register update stress. Motors remain
 *                        disabled or have zero duty, so output pins stay low.
 * sample <seconds> [us]  Poll SENSOR_RAW and exercise sample-to-read latency.
 * snapshot               Print the PL performance register set.
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static constexpr size_t kMapSize = 0x1000;
static constexpr uint32_t kOffMotor1Ctrl = 0x40;
static constexpr uint32_t kOffMotor1Pwm = 0x44;
static constexpr uint32_t kOffMotor2Ctrl = 0x48;
static constexpr uint32_t kOffMotor2Pwm = 0x4C;
static constexpr uint32_t kOffSensorRaw = 0x68;

static constexpr uint32_t kOffPerfCtrl = 0x100;
static constexpr uint32_t kOffPerfStatus = 0x104;
static constexpr uint32_t kOffTimestampLow = 0x108;
static constexpr uint32_t kOffTimestampHigh = 0x10C;
static constexpr uint32_t kOffControlWrites = 0x110;
static constexpr uint32_t kOffControlEvents = 0x114;
static constexpr uint32_t kOffControlLast = 0x118;
static constexpr uint32_t kOffControlMin = 0x11C;
static constexpr uint32_t kOffControlMax = 0x120;
static constexpr uint32_t kOffControlSumLow = 0x124;
static constexpr uint32_t kOffControlSumHigh = 0x128;
static constexpr uint32_t kOffControlOverflows = 0x12C;
static constexpr uint32_t kOffSampleIntervalEvents = 0x130;
static constexpr uint32_t kOffSampleIntervalLast = 0x134;
static constexpr uint32_t kOffSampleIntervalMin = 0x138;
static constexpr uint32_t kOffSampleIntervalMax = 0x13C;
static constexpr uint32_t kOffSampleIntervalSumLow = 0x140;
static constexpr uint32_t kOffSampleIntervalSumHigh = 0x144;
static constexpr uint32_t kOffSampleTimeouts = 0x148;
static constexpr uint32_t kOffSamplePendingOverwrites = 0x14C;
static constexpr uint32_t kOffSampleJitterLast = 0x150;
static constexpr uint32_t kOffSampleJitterMin = 0x154;
static constexpr uint32_t kOffSampleJitterMax = 0x158;
static constexpr uint32_t kOffSampleJitterSumLow = 0x15C;
static constexpr uint32_t kOffSampleJitterSumHigh = 0x160;
static constexpr uint32_t kOffSampleReadEvents = 0x164;
static constexpr uint32_t kOffSampleReadLast = 0x168;
static constexpr uint32_t kOffSampleReadMin = 0x16C;
static constexpr uint32_t kOffSampleReadMax = 0x170;
static constexpr uint32_t kOffSampleReadSumLow = 0x174;
static constexpr uint32_t kOffSampleReadSumHigh = 0x178;
static constexpr uint32_t kOffSampleExpectedPeriod = 0x17C;
static constexpr uint32_t kOffSampleTimeoutThreshold = 0x180;
static constexpr uint32_t kOffPerfClockHz = 0x184;
static constexpr uint32_t kOffSampleReadMisses = 0x188;
static constexpr uint32_t kOffSampleValidCount = 0x18C;
static constexpr uint32_t kOffControlHist = 0x1A0;
static constexpr uint32_t kOffSampleReadHist = 0x220;
static constexpr uint32_t kOffSampleIntervalHist = 0x2A0;

static volatile uint32_t* g_regs = nullptr;

static uint32_t parse_u32(const char* text, const char* what) {
    char* end = nullptr;
    errno = 0;
    unsigned long value = std::strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        value > 0xFFFFFFFFUL) {
        std::fprintf(stderr, "invalid %s: %s\n", what, text);
        std::exit(1);
    }
    return static_cast<uint32_t>(value);
}

static void map_pl(uint32_t base_phys) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        std::perror("open /dev/mem");
        std::exit(1);
    }
    void* mapped = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, static_cast<off_t>(base_phys));
    close(fd);
    if (mapped == MAP_FAILED) {
        std::perror("mmap");
        std::exit(1);
    }
    g_regs = reinterpret_cast<volatile uint32_t*>(mapped);
}

static uint32_t rd(uint32_t offset) {
    return g_regs[offset / 4];
}

static void wr(uint32_t offset, uint32_t value) {
    g_regs[offset / 4] = value;
    __sync_synchronize();
}

static uint64_t rd64(uint32_t low_offset, uint32_t high_offset) {
    const uint64_t low = rd(low_offset);
    const uint64_t high = rd(high_offset);
    return low | (high << 32);
}

static void clear_perf() {
    wr(kOffPerfCtrl, 0x3);
}

static void print_hist(const char* name, uint32_t base_offset) {
    std::printf("\"%s\":[", name);
    for (unsigned index = 0; index < 32; ++index) {
        if (index != 0)
            std::printf(",");
        std::printf("%u", rd(base_offset + index * 4));
    }
    std::printf("]");
}

static void print_common() {
    std::printf(
        "\"perf_status\":%u,"
        "\"clk_hz\":%u,"
        "\"timestamp\":%llu,"
        "\"control_writes\":%u,"
        "\"control_events\":%u,"
        "\"control_last\":%u,"
        "\"control_min\":%u,"
        "\"control_max\":%u,"
        "\"control_sum\":%llu,"
        "\"control_overflows\":%u,"
        "\"sample_valid_count\":%u,"
        "\"sample_interval_events\":%u,"
        "\"sample_interval_last\":%u,"
        "\"sample_interval_min\":%u,"
        "\"sample_interval_max\":%u,"
        "\"sample_interval_sum\":%llu,"
        "\"sample_timeouts\":%u,"
        "\"sample_pending_overwrites\":%u,"
        "\"sample_jitter_last\":%u,"
        "\"sample_jitter_min\":%u,"
        "\"sample_jitter_max\":%u,"
        "\"sample_jitter_sum\":%llu,"
        "\"sample_read_events\":%u,"
        "\"sample_read_last\":%u,"
        "\"sample_read_min\":%u,"
        "\"sample_read_max\":%u,"
        "\"sample_read_sum\":%llu,"
        "\"sample_read_misses\":%u,"
        "\"sample_expected_period\":%u,"
        "\"sample_timeout_threshold\":%u,",
        rd(kOffPerfStatus),
        rd(kOffPerfClockHz),
        static_cast<unsigned long long>(
            rd64(kOffTimestampLow, kOffTimestampHigh)),
        rd(kOffControlWrites),
        rd(kOffControlEvents),
        rd(kOffControlLast),
        rd(kOffControlMin),
        rd(kOffControlMax),
        static_cast<unsigned long long>(
            rd64(kOffControlSumLow, kOffControlSumHigh)),
        rd(kOffControlOverflows),
        rd(kOffSampleValidCount),
        rd(kOffSampleIntervalEvents),
        rd(kOffSampleIntervalLast),
        rd(kOffSampleIntervalMin),
        rd(kOffSampleIntervalMax),
        static_cast<unsigned long long>(
            rd64(kOffSampleIntervalSumLow, kOffSampleIntervalSumHigh)),
        rd(kOffSampleTimeouts),
        rd(kOffSamplePendingOverwrites),
        rd(kOffSampleJitterLast),
        rd(kOffSampleJitterMin),
        rd(kOffSampleJitterMax),
        static_cast<unsigned long long>(
            rd64(kOffSampleJitterSumLow, kOffSampleJitterSumHigh)),
        rd(kOffSampleReadEvents),
        rd(kOffSampleReadLast),
        rd(kOffSampleReadMin),
        rd(kOffSampleReadMax),
        static_cast<unsigned long long>(
            rd64(kOffSampleReadSumLow, kOffSampleReadSumHigh)),
        rd(kOffSampleReadMisses),
        rd(kOffSampleExpectedPeriod),
        rd(kOffSampleTimeoutThreshold));
    print_hist("control_hist", kOffControlHist);
    std::printf(",");
    print_hist("sample_read_hist", kOffSampleReadHist);
    std::printf(",");
    print_hist("sample_interval_hist", kOffSampleIntervalHist);
}

static int cmd_controls(uint32_t count) {
    wr(kOffMotor1Ctrl, 0);
    wr(kOffMotor1Pwm, 0);
    wr(kOffMotor2Ctrl, 0);
    wr(kOffMotor2Pwm, 0);
    // Start from the opposite state so every test write changes the register.
    // Motor 2 PWM is zero, so enabling it produces no output pulse.
    wr(kOffMotor2Ctrl, 1);
    clear_perf();

    for (uint32_t i = 0; i < count; ++i) {
        if ((i & 1u) == 0) {
            wr(kOffMotor1Pwm, ((i >> 1) & 1u) ? 1u : 2u);
        } else {
            wr(kOffMotor2Ctrl, ((i >> 1) & 1u) ? 1u : 0u);
        }
    }
    usleep(2000);

    std::printf(
        "{\"command\":\"controls\",\"iterations\":%u,"
        "\"motors_safe\":true,",
        count);
    print_common();
    std::printf("}\n");

    wr(kOffMotor1Ctrl, 0);
    wr(kOffMotor1Pwm, 0);
    wr(kOffMotor2Ctrl, 0);
    wr(kOffMotor2Pwm, 0);
    return 0;
}

static uint64_t monotonic_ns() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<uint64_t>(now.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(now.tv_nsec);
}

static int cmd_sample(uint32_t seconds, uint32_t poll_us) {
    if (seconds == 0 || poll_us == 0) {
        std::fprintf(stderr, "seconds and poll_us must be positive\n");
        return 1;
    }
    clear_perf();
    const uint64_t start_ns = monotonic_ns();
    const uint64_t duration_ns = static_cast<uint64_t>(seconds) * 1000000000ULL;
    uint64_t reads = 0;
    while (monotonic_ns() - start_ns < duration_ns) {
        (void)rd(kOffSensorRaw);
        ++reads;
        usleep(poll_us);
    }
    const uint64_t elapsed_ns = monotonic_ns() - start_ns;
    std::printf(
        "{\"command\":\"sample\",\"seconds\":%u,\"poll_us\":%u,"
        "\"wall_ns\":%llu,\"raw_reads\":%llu,",
        seconds, poll_us,
        static_cast<unsigned long long>(elapsed_ns),
        static_cast<unsigned long long>(reads));
    print_common();
    std::printf("}\n");
    return 0;
}

static int cmd_snapshot() {
    std::printf("{\"command\":\"snapshot\",\"version\":%u,", rd(0x30));
    print_common();
    std::printf("}\n");
    return 0;
}

static int cmd_canloop(uint32_t count) {
    const int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        std::perror("socket(PF_CAN)");
        return 1;
    }
    int buf_size = 1 << 20;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));

    ifreq ifr{};
    std::strncpy(ifr.ifr_name, "can0", IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        std::perror("ioctl(SIOCGIFINDEX)");
        close(fd);
        return 1;
    }
    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        std::perror("bind(can0)");
        close(fd);
        return 1;
    }

    can_frame tx{};
    tx.can_id = 0x123;
    tx.can_dlc = 4;
    tx.data[0] = 0x11;
    tx.data[1] = 0x22;
    tx.data[2] = 0x33;
    tx.data[3] = 0x44;

    uint32_t sent = 0;
    uint32_t received = 0;
    const uint64_t start_ns = monotonic_ns();
    for (uint32_t index = 0; index < count; ++index) {
        while (write(fd, &tx, sizeof(tx)) < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != ENOBUFS) {
                std::perror("write(can0)");
                close(fd);
                return 1;
            }
        }
        ++sent;
        pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 1000) <= 0)
            continue;
        can_frame rx{};
        const ssize_t length = read(fd, &rx, sizeof(rx));
        if (length == static_cast<ssize_t>(sizeof(rx)) &&
            rx.can_id == tx.can_id && rx.can_dlc == tx.can_dlc &&
            std::memcmp(rx.data, tx.data, tx.can_dlc) == 0) {
            ++received;
        }
    }
    const uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close(fd);
    std::printf(
        "{\"command\":\"canloop\",\"requested\":%u,\"sent\":%u,"
        "\"received\":%u,\"wall_ns\":%llu}\n",
        count, sent, received,
        static_cast<unsigned long long>(elapsed_ns));
    return sent == count && received == count ? 0 : 1;
}

static void usage(const char* program) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s controls <count>\n"
        "  %s sample <seconds> [poll_us]\n"
        "  %s canloop <count>\n"
        "  %s snapshot\n",
        program, program, program, program);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    const char* base_text = std::getenv("PL_REG_BASE");
    const uint32_t base = base_text
        ? parse_u32(base_text, "PL_REG_BASE")
        : 0x80000000U;
    map_pl(base);

    const std::string command = argv[1];
    if (command == "controls" && argc == 3) {
        return cmd_controls(parse_u32(argv[2], "count"));
    }
    if (command == "sample" && (argc == 3 || argc == 4)) {
        const uint32_t poll_us = argc == 4
            ? parse_u32(argv[3], "poll_us")
            : 5000U;
        return cmd_sample(parse_u32(argv[2], "seconds"), poll_us);
    }
    if (command == "canloop" && argc == 3) {
        return cmd_canloop(parse_u32(argv[2], "count"));
    }
    if (command == "snapshot" && argc == 2) {
        return cmd_snapshot();
    }
    usage(argv[0]);
    return 1;
}
