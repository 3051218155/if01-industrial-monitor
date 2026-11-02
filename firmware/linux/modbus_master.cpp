/*
 * modbus_master — IF-01 PL RS485 Modbus RTU 主站（FC03 读保持寄存器）
 *
 * 依赖 PL 寄存器（9600 8N1，板载 RS485 @ L17/N16/N15）：
 *   0x50 TX_DATA  写低 8 位发送一字节
 *   0x54 RX_DATA  读低 8 位弹出一字节
 *   0x58 STATUS   bit0 TX_BUSY bit1 RX_VALID bit2 RX_OVERRUN bit3 DE
 *   0x5C CTRL     bit0 DE_FORCE bit1 AUTO_DE(默认1) bit2 RX_EN bit3 TX_EN
 *
 * 用法：
 *   PL_REG_BASE=0x80000000 ./modbus_master read03 <slave> <start> <count>
 *
 * PC 侧 Modbus Slave：9600 8N1，从站 1，A0/B0/GND 接板子。
 */

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static constexpr size_t  kMapSize = 0x1000;
static constexpr off_t   kOffTx   = 0x50;
static constexpr off_t   kOffRx   = 0x54;
static constexpr off_t   kOffStat = 0x58;
static constexpr off_t   kOffCtrl = 0x5C;
static constexpr off_t   kOffParserCtrl = 0x90;
static constexpr off_t   kOffFrameIndex = 0xF8;
static constexpr off_t   kOffFrameCtrl = 0xFC;
static constexpr uint16_t kParserRegisterCount = 12;

static constexpr uint32_t kStatTxBusy    = 1u << 0;
static constexpr uint32_t kStatRxValid   = 1u << 1;
static constexpr uint32_t kStatRxOverrun = 1u << 2;
static constexpr uint32_t kStatDe        = 1u << 3;

static constexpr uint32_t kCtrlDeForce = 1u << 0;
static constexpr uint32_t kCtrlAutoDe  = 1u << 1;
static constexpr uint32_t kCtrlRxEn    = 1u << 2;
static constexpr uint32_t kCtrlTxEn    = 1u << 3;

static volatile uint32_t* g_regs = nullptr;
static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int) {
    g_stop = 1;
}

static uint32_t parse_u32(const char* s, const char* what) {
    char* end = nullptr;
    errno = 0;
    unsigned long v = std::strtoul(s, &end, 0);
    if (errno != 0 || end == s || *end != '\0' || v > 0xFFFFFFFFUL) {
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
    void* mapped = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE, MAP_SHARED,
                        fd, static_cast<off_t>(base_phys));
    close(fd);
    if (mapped == MAP_FAILED) {
        std::perror("mmap");
        std::exit(1);
    }
    return reinterpret_cast<volatile uint32_t*>(mapped);
}

static uint32_t reg_read(off_t byte_off) {
    return g_regs[byte_off / 4];
}

static void reg_write(off_t byte_off, uint32_t val) {
    g_regs[byte_off / 4] = val;
}

static void rs485_init() {
    reg_write(kOffCtrl, kCtrlAutoDe | kCtrlRxEn | kCtrlTxEn);
}

static void configure_parser(uint8_t slave) {
    const uint32_t ctrl =
        1u | 2u |
        (static_cast<uint32_t>(slave) << 8) |
        (static_cast<uint32_t>(kParserRegisterCount) << 16);
    reg_write(kOffParserCtrl, ctrl);
}

static bool rs485_tx_byte(uint8_t b) {
    for (int i = 0; i < 5000; ++i) {
        if ((reg_read(kOffStat) & kStatTxBusy) == 0)
            break;
        usleep(100);
    }
    reg_write(kOffTx, b);
    for (int i = 0; i < 5000; ++i) {
        if ((reg_read(kOffStat) & kStatTxBusy) == 0)
            return true;
        usleep(100);
    }
    std::fprintf(stderr, "TX timeout byte 0x%02X\n", b);
    return false;
}

static void rs485_flush_input() {
    // Drop any stale byte left from a previous transaction or bus glitch.
    for (int i = 0; i < 256; ++i) {
        uint32_t st = reg_read(kOffStat);
        if (!(st & kStatRxValid))
            break;
        (void)reg_read(kOffRx);
    }
}

static void acknowledge_stale_frames() {
    for (int i = 0; i < 16; ++i) {
        uint32_t status = reg_read(kOffFrameCtrl);
        if (!(status & 1u))
            break;
        reg_write(kOffFrameCtrl, 1u);
    }
}

static bool read_valid_frame(std::vector<uint8_t>& frame, int timeout_ms) {
    frame.clear();
    int loops = timeout_ms * 10;
    while (loops-- > 0) {
        const uint32_t status = reg_read(kOffFrameCtrl);
        if (status & 1u) {
            const size_t len = (status >> 8) & 0xFFu;
            if (len == 0 || len > 256) {
                reg_write(kOffFrameCtrl, 1u);
                return false;
            }
            frame.resize(len);
            for (size_t word = 0; word * 4 < len; ++word) {
                reg_write(kOffFrameIndex, static_cast<uint32_t>(word));
                __sync_synchronize();
                const uint32_t data = reg_read(kOffFrameIndex);
                for (size_t byte = 0; byte < 4; ++byte) {
                    const size_t index = word * 4 + byte;
                    if (index < len)
                        frame[index] = static_cast<uint8_t>(data >> (byte * 8));
                }
            }
            reg_write(kOffFrameCtrl, 1u);
            return true;
        }
        usleep(100);
    }
    return false;
}

static uint16_t modbus_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1)
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001);
            else
                crc = static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

static void build_read03(uint8_t slave, uint16_t start, uint16_t count, std::vector<uint8_t>& frame) {
    frame = {
        slave,
        0x03,
        static_cast<uint8_t>((start >> 8) & 0xFF),
        static_cast<uint8_t>(start & 0xFF),
        static_cast<uint8_t>((count >> 8) & 0xFF),
        static_cast<uint8_t>(count & 0xFF),
    };
    uint16_t crc = modbus_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

static bool read03_once(uint8_t slave, uint16_t start, uint16_t count,
                        std::vector<uint16_t>& regs, bool verbose) {
    regs.clear();
    std::vector<uint8_t> req;
    build_read03(slave, start, count, req);
    configure_parser(slave);

    if (verbose) {
        std::printf("TX (%zu): ", req.size());
        for (uint8_t b : req)
            std::printf("%02X ", b);
        std::printf("\n");
    }

    rs485_flush_input();
    acknowledge_stale_frames();
    for (uint8_t b : req)
        if (!rs485_tx_byte(b))
            return false;

    usleep(5000);

    std::vector<uint8_t> resp;
    if (!read_valid_frame(resp, 500)) {
        if (verbose)
            std::fprintf(stderr, "FAIL: no hardware-validated response frame\n");
        return false;
    }

    if (verbose) {
        std::printf("RX (%zu): ", resp.size());
        for (uint8_t x : resp)
            std::printf("%02X ", x);
        std::printf("\n");
    }

    if (resp.size() < 5) {
        if (verbose)
            std::fprintf(stderr, "FAIL: response too short\n");
        return false;
    }

    uint16_t crc_calc = modbus_crc16(resp.data(), resp.size() - 2);
    uint16_t crc_recv = static_cast<uint16_t>(resp[resp.size() - 2]) |
                        (static_cast<uint16_t>(resp[resp.size() - 1]) << 8);
    if (crc_calc != crc_recv) {
        if (verbose)
            std::fprintf(stderr, "FAIL: CRC recv=0x%04X calc=0x%04X\n", crc_recv, crc_calc);
        return false;
    }
    if (resp[0] != slave || resp[1] != 0x03) {
        if (verbose)
            std::fprintf(stderr, "FAIL: unexpected header slave/fc\n");
        return false;
    }

    uint8_t byte_count = resp[2];
    if (resp.size() != static_cast<size_t>(3 + byte_count + 2)) {
        if (verbose)
            std::fprintf(stderr, "FAIL: length mismatch byte_count=%u\n", byte_count);
        return false;
    }

    for (unsigned i = 0; i + 1 < byte_count; i += 2) {
        uint16_t reg = static_cast<uint16_t>(resp[3 + i] << 8) | resp[3 + i + 1];
        regs.push_back(reg);
    }
    return true;
}

static bool read03(uint8_t slave, uint16_t start, uint16_t count) {
    std::vector<uint16_t> regs;
    if (!read03_once(slave, start, count, regs, true))
        return false;

    std::printf("OK: FC03 slave=%u start=0x%04X count=%u\n", slave, start, count);
    for (size_t i = 0; i < regs.size(); ++i) {
        std::printf("  reg[0x%04X] = 0x%04X (%u)\n",
                    static_cast<unsigned>(start + i), regs[i], regs[i]);
    }
    return true;
}

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static int poll_modbus(uint8_t slave, uint16_t start, uint16_t count,
                       int period_ms, int seconds, const std::string& csv_path) {
    std::ofstream csv;
    bool csv_ready = false;
    if (!csv_path.empty()) {
        csv.open(csv_path, std::ios::app);
        if (!csv.is_open()) {
            std::fprintf(stderr, "warn: cannot open CSV %s\n", csv_path.c_str());
        } else {
            csv.seekp(0, std::ios::end);
            if (csv.tellp() == 0)
                csv << "timestamp_ms,slave,start,count,values,crc_ok\n";
            csv_ready = true;
        }
    }

    const auto t_begin = std::chrono::steady_clock::now();
    int ok_count = 0;
    int fail_count = 0;
    int total = 0;

    std::printf("[poll] slave=%u start=0x%04X count=%u period=%dms csv=%s\n",
                slave, start, count, period_ms, csv_path.c_str());

    while (!g_stop) {
        if (seconds > 0) {
            auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - t_begin)
                                 .count();
            if (elapsed_s >= seconds)
                break;
        }

        const auto tick_begin = std::chrono::steady_clock::now();
        std::vector<uint16_t> regs;
        bool ok = read03_once(slave, start, count, regs, false);
        int64_t ts = now_ms();
        ++total;

        if (ok) {
            ++ok_count;
            std::printf("[%04d] OK  t=%lld", total, static_cast<long long>(ts));
            for (size_t i = 0; i < regs.size(); ++i)
                std::printf(" reg%zu=%u", i, regs[i]);
            std::printf("\n");
        } else {
            ++fail_count;
            std::printf("[%04d] FAIL t=%lld\n", total, static_cast<long long>(ts));
        }
        std::fflush(stdout);

        if (csv_ready) {
            csv << ts << ',' << static_cast<unsigned>(slave) << ','
                << start << ',' << count << ',';
            for (size_t i = 0; i < regs.size(); ++i) {
                if (i)
                    csv << ':';
                csv << regs[i];
            }
            csv << ',' << (ok ? 1 : 0) << '\n';
            csv.flush();
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - tick_begin)
                           .count();
        if (elapsed < period_ms)
            usleep(static_cast<useconds_t>((period_ms - elapsed) * 1000));
    }

    std::printf("[poll] done total=%d ok=%d fail=%d\n", total, ok_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}

static void usage(const char* prog) {
    std::fprintf(stderr,
                "Usage:\n"
                "  %s read03 <slave> <start_reg> <count>\n"
                "  %s poll <slave> <start_reg> <count> <period_ms> "
                "[--seconds N] [--csv /root/modbus_poll.csv]\n"
                "\n"
                "  PL_REG_BASE env, default 0x80000000\n"
                "  Requires IF-01 PL with RS485 regs @ 0x50~0x5C (9600 8N1)\n",
                prog, prog);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const uint32_t base_phys = []() {
        const char* env = std::getenv("PL_REG_BASE");
        return env ? parse_u32(env, "PL_REG_BASE") : 0x80000000u;
    }();

    g_regs = map_pl_base(base_phys);
    rs485_init();

    if (std::strcmp(argv[1], "read03") == 0) {
        if (argc < 5) {
            usage(argv[0]);
            return 1;
        }
        uint8_t slave = static_cast<uint8_t>(parse_u32(argv[2], "slave"));
        uint16_t start = static_cast<uint16_t>(parse_u32(argv[3], "start"));
        uint16_t count = static_cast<uint16_t>(parse_u32(argv[4], "count"));
        return read03(slave, start, count) ? 0 : 1;
    }

    if (std::strcmp(argv[1], "poll") == 0) {
        if (argc < 6) {
            usage(argv[0]);
            return 1;
        }
        uint8_t slave = static_cast<uint8_t>(parse_u32(argv[2], "slave"));
        uint16_t start = static_cast<uint16_t>(parse_u32(argv[3], "start"));
        uint16_t count = static_cast<uint16_t>(parse_u32(argv[4], "count"));
        int period_ms = static_cast<int>(parse_u32(argv[5], "period_ms"));
        int seconds = 0;
        std::string csv_path = "/root/modbus_poll.csv";

        for (int i = 6; i < argc; ++i) {
            if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
                seconds = static_cast<int>(parse_u32(argv[++i], "seconds"));
            } else if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
                csv_path = argv[++i];
            } else {
                usage(argv[0]);
                return 1;
            }
        }
        if (count < 1 || count > 125 || period_ms < 10) {
            std::fprintf(stderr, "invalid poll parameters\n");
            return 1;
        }
        return poll_modbus(slave, start, count, period_ms, seconds, csv_path);
    }

    usage(argv[0]);
    return 1;
}
