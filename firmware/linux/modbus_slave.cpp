/*
 * modbus_slave - board-side multi-node Modbus RTU service for the PC HMI.
 *
 * Transport:
 *   IF-01 PL RS485, 9600 8N1, PL_REG_BASE default 0x80000000
 *   0x50 TX_DATA, 0x54 RX_DATA, 0x58 STATUS, 0x5C CTRL
 *
 * Supported functions:
 *   0x03 read holding registers
 *   0x06 write single holding register
 *
 * Register map:
 *   0  motor A control  0x40  R/W
 *   1  motor A PWM      0x44  R/W (0..1000)
 *   2  accelerometer X  0x68  R
 *   3  accelerometer Y  0x320 R
 *   4  accelerometer Z  0x324 R
 *   5  MPU temperature  0x328 R
 *   6  gyroscope X      0x32C R
 *   7  gyroscope Y      0x330 R
 *   8  gyroscope Z      0x334 R
 *   9  data flags       generated; bits13:11 select 3D fault site
 *   10 alert low        0x80  R/W
 *   11 alert high       0x84  R/W
 *   12 vibration RMS    0x70  R
 *   13 vibration peak   0x74  R
 *   14 sensor WHO_AM_I  0x88  R
 *   15 current A         simulated from motor A enable/PWM (0.01 A)
 *   16..20 five vibration points for each logical motor
 *   21 board-side point-location result: level/site
 *   22..29 board-side model result: condition/confidence/latency/probabilities
 *   30 model validity flags
 *   31 logical device ID
 *
 * The same serial port answers logical slave IDs 1..6. ID1 mirrors the real
 * PL/MPU channels; IDs2..6 use independent board-side simulated state.
 *
 * Usage:
 *   PL_REG_BASE=0x80000000 ./modbus_slave [--csv /root/serial_hmi_log.csv]
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <pthread.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr size_t kMapSize = 0x1000;
static constexpr off_t kOffTx = 0x50;
static constexpr off_t kOffRx = 0x54;
static constexpr off_t kOffStat = 0x58;
static constexpr off_t kOffCtrl = 0x5C;
static constexpr off_t kOffParserCtrl = 0x90;
static constexpr off_t kOffFrameIndex = 0xF8;
static constexpr off_t kOffFrameCtrl = 0xFC;

static constexpr uint32_t kStatTxBusy = 1u << 0;
static constexpr uint32_t kStatRxValid = 1u << 1;
static constexpr uint32_t kStatRxOverrun = 1u << 2;

static constexpr uint32_t kCtrlAutoDe = 1u << 1;
static constexpr uint32_t kCtrlRxEn = 1u << 2;
static constexpr uint32_t kCtrlTxEn = 1u << 3;

static constexpr uint16_t kFlagSampleValid = 1u << 0;
static constexpr uint16_t kFlagFeatureValid = 1u << 1;
static constexpr uint16_t kFlagMpuBusy = 1u << 2;
static constexpr uint16_t kFlagMpuError = 1u << 3;
static constexpr uint16_t kFlagSourceReal = 1u << 4;
static constexpr uint16_t kFlagSourceSim = 1u << 5;
static constexpr uint16_t kFlagMotorAReal = 1u << 6;
static constexpr uint16_t kFlagLocalDevice = 1u << 7;
static constexpr uint16_t kFlagNpuDeployed = 1u << 8;
static constexpr uint16_t kFlagNpuModelReady = 1u << 9;

static volatile uint32_t* g_regs = nullptr;
static volatile sig_atomic_t g_stop = 0;
static uint16_t g_fault_site_code = 0;

static constexpr uint16_t kDeviceCount = 6;
static constexpr uint16_t kDeviceRegisterCount = 32;
static constexpr uint16_t kModelConditionRegister = 22;
static constexpr uint16_t kModelConfidenceRegister = 23;
static constexpr uint16_t kModelLatencyRegister = 24;
static constexpr uint16_t kModelProbabilityRegister = 25;
static constexpr uint16_t kModelFlagsRegister = 30;
static constexpr uint16_t kDeviceIdentityRegister = 31;

struct ModelRegisters {
    std::array<std::atomic<uint16_t>, 9> values{};

    ModelRegisters() {
        for (auto& value : values)
            value.store(0, std::memory_order_relaxed);
    }
};

struct DeviceState {
    std::array<uint16_t, kDeviceRegisterCount> regs{};
    ModelRegisters model;
};

static std::array<DeviceState, kDeviceCount> g_devices{};
static std::array<std::atomic<uint32_t>, kDeviceCount> g_can_override_mask{};
static std::array<std::atomic<bool>, kDeviceCount> g_can_model_override{};
static std::atomic<uint32_t> g_model_refresh_mask{0};
static std::recursive_mutex g_state_mutex;

struct RegisterDef {
    off_t pl_off;
    bool writable;
};

static const RegisterDef kRegisters[] = {
    {0x40, true},   // 0: motor A control
    {0x44, true},   // 1: motor A PWM
    {0x68, false},  // 2: accelerometer X
    {0x320, false}, // 3: accelerometer Y
    {0x324, false}, // 4: accelerometer Z
    {0x328, false}, // 5: MPU temperature
    {0x32C, false}, // 6: gyroscope X
    {0x330, false}, // 7: gyroscope Y
    {0x334, false}, // 8: gyroscope Z
    {0, false},     // 9: generated data flags
    {0x80, true},   // 10: alert low threshold
    {0x84, true},   // 11: alert high threshold
    {0x70, false},  // 12: vibration RMS
    {0x74, false},  // 13: vibration peak
    {0x88, false},  // 14: sensor WHO_AM_I
    {0, false},     // 15: generated motor A current
};

static constexpr uint16_t kPhysicalRegisterCount =
    static_cast<uint16_t>(sizeof(kRegisters) / sizeof(kRegisters[0]));
static constexpr uint16_t kDataFlagsRegister = 9;
static constexpr uint16_t kCurrentRegister = 15;
static constexpr uint16_t kFirstSimulatedPointRegister = 16;
static constexpr uint16_t kSimulatedPointCount = 5;
static constexpr uint16_t kPointSiteStatusRegister = 21;
static constexpr uint16_t kRegisterCount = kDeviceRegisterCount;
static constexpr uint16_t kPointWarn = 250;   // m/s^2 x100
static constexpr uint16_t kPointFault = 600;  // m/s^2 x100
static constexpr uint16_t kRmsWarnRaw = 3000;
static constexpr uint16_t kRmsFaultRaw = 4500;

static void on_sigint(int) {
    g_stop = 1;
}

static void prioritize_serial_thread() {
    sched_param param{};
    param.sched_priority = 40;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0)
        std::fprintf(stderr,
                     "warn: cannot enable SCHED_FIFO for serial thread: %s\n",
                     std::strerror(errno));
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
    __sync_synchronize();
}

static void rs485_init() {
    reg_write(kOffCtrl, kCtrlAutoDe | kCtrlRxEn | kCtrlTxEn);
}

static void configure_parser(uint8_t slave_id, uint16_t register_count) {
    const uint32_t ctrl =
        1u |
        (static_cast<uint32_t>(slave_id) << 8) |
        (static_cast<uint32_t>(register_count) << 16);
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

static bool rs485_tx(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (!rs485_tx_byte(data[i]))
            return false;
    }
    return true;
}

static void rs485_flush_input() {
    for (int i = 0; i < 256; ++i) {
        uint32_t st = reg_read(kOffStat);
        if (!(st & kStatRxValid))
            break;
        (void)reg_read(kOffRx);
    }
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

static void append_crc(std::vector<uint8_t>& frame) {
    uint16_t crc = modbus_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

static bool frame_crc_ok(const uint8_t* frame, size_t len) {
    if (len < 4)
        return false;
    uint16_t recv = static_cast<uint16_t>(frame[len - 2]) |
                    (static_cast<uint16_t>(frame[len - 1]) << 8);
    return modbus_crc16(frame, len - 2) == recv;
}

static void make_exception(uint8_t slave, uint8_t function, uint8_t code,
                           std::vector<uint8_t>& response) {
    response = {slave, static_cast<uint8_t>(function | 0x80), code};
    append_crc(response);
}

static void acknowledge_stale_frames() {
    for (int i = 0; i < 16; ++i) {
        uint32_t status = reg_read(kOffFrameCtrl);
        if (!(status & 1u))
            break;
        reg_write(kOffFrameCtrl, 1u);
    }
}

static bool read_valid_frame(uint8_t* frame, size_t capacity,
                             size_t* frame_len, int timeout_ms) {
    int loops = timeout_ms * 10;
    while (loops-- > 0) {
        uint32_t status = reg_read(kOffFrameCtrl);
        if (status & 1u) {
            const size_t len = (status >> 8) & 0xFFu;
            if (len == 0 || len > capacity) {
                reg_write(kOffFrameCtrl, 1u);
                return false;
            }
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
            *frame_len = len;
            return true;
        }
        usleep(100);
    }
    return false;
}

static bool read_request(uint8_t frame[8], int timeout_ms) {
    uint8_t captured[256] = {};
    size_t length = 0;
    if (!read_valid_frame(captured, sizeof(captured), &length, timeout_ms))
        return false;
    if (length != 8)
        return false;
    std::memcpy(frame, captured, 8);
    return true;
}

static bool known_mpu_id(uint8_t id) {
    switch (id) {
        case 0x68: case 0x70: case 0x71: case 0x72:
        case 0x73: case 0x74: case 0x75: case 0x98:
            return true;
        default:
            return false;
    }
}

static uint16_t device_rms(uint16_t slave)
{
    return slave == 1
        ? static_cast<uint16_t>(reg_read(0x70) & 0xFFFF)
        : g_devices[slave - 1].regs[12];
}

static uint16_t device_alert_status(uint16_t slave)
{
    std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
    auto& state = g_devices[slave - 1];
    uint16_t max_point = 0;
    int max_site = 0;
    for (uint16_t i = 0; i < kSimulatedPointCount; ++i) {
        const uint16_t value =
            state.regs[kFirstSimulatedPointRegister + i];
        if (value > max_point) {
            max_point = value;
            max_site = static_cast<int>(i + 1);
        }
    }

    const int point_level = max_point >= kPointFault ? 2
        : max_point >= kPointWarn ? 1 : 0;
    const uint16_t rms = device_rms(slave);
    // 0x80/0x84 are also used as a forced output selector for the physical
    // buzzer/RGB chain. Analyze the signal against fixed engineering limits
    // so that forcing an output cannot feed back as a new sensor alarm.
    const int rms_level = rms >= kRmsFaultRaw ? 2
        : rms >= kRmsWarnRaw ? 1 : 0;
    const int level = std::max(point_level, rms_level);
    const int site = point_level > 0 ? max_site : (rms_level > 0 ? 5 : 0);
    return static_cast<uint16_t>((level << 8) | site);
}

static void initialize_devices()
{
    static constexpr uint16_t kDefaults[kDeviceCount][5] = {
        {80, 70, 90, 60, 100},
        {95, 82, 76, 88, 74},
        {105, 118, 92, 87, 110},
        {72, 89, 103, 96, 84},
        {88, 74, 107, 91, 79},
        {111, 86, 78, 102, 93},
    };
    for (uint16_t slave = 1; slave <= kDeviceCount; ++slave) {
        auto& state = g_devices[slave - 1];
        state.regs.fill(0);
        state.regs[10] = 3000;
        state.regs[11] = 4500;
        state.regs[0] = slave == 1 ? 0 : 1;
        state.regs[1] = slave == 1 ? 0 : 950;
        state.regs[5] = 7345;
        state.regs[12] = slave == 1 ? 1500 : 100;
        state.regs[13] = slave == 1 ? 5000 : 160;
        state.regs[14] = 0x74;
        state.regs[15] = slave == 1 ? 75 : 380;
        state.regs[kDeviceIdentityRegister] = slave;
        for (uint16_t i = 0; i < kSimulatedPointCount; ++i)
            state.regs[kFirstSimulatedPointRegister + i] =
                kDefaults[slave - 1][i];
    }
}

static uint16_t read_model_register(uint16_t slave, uint16_t index)
{
    const uint16_t model_index = index - kModelConditionRegister;
    return g_devices[slave - 1].model.values[model_index]
        .load(std::memory_order_relaxed);
}

static bool local_mpu_healthy_unlocked()
{
    const uint32_t status = reg_read(0x64);
    const uint8_t whoami = static_cast<uint8_t>(reg_read(0x88) & 0xFFu);
    return known_mpu_id(whoami) && !(status & (1u << 3));
}

static uint16_t read_register(uint16_t slave, uint16_t index) {
    std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
    if (slave < 1 || slave > kDeviceCount ||
        index >= kDeviceRegisterCount)
        return 0;
    if (index >= kModelConditionRegister &&
        index < kModelConditionRegister + 8)
        return read_model_register(slave, index);
    if (index == kModelFlagsRegister)
        return read_model_register(slave, kModelFlagsRegister);
    if (index == kDeviceIdentityRegister)
        return slave;
    if (index == kPointSiteStatusRegister)
        return device_alert_status(slave);

    auto& state = g_devices[slave - 1];
    const uint32_t can_override =
        g_can_override_mask[slave - 1].load(std::memory_order_relaxed);
    if (slave == 1) {
        if (index == kCurrentRegister) {
            if (can_override & (1u << kCurrentRegister))
                return state.regs[kCurrentRegister];
            const bool enabled = (reg_read(0x40) & 1u) != 0;
            const uint32_t pwm = std::min<uint32_t>(reg_read(0x44), 1000u);
            if (!enabled || pwm == 0)
                return 0;
            return static_cast<uint16_t>(
                std::min<uint32_t>(120u, 25u + pwm * 80u / 1000u));
        }
        if (index == kDataFlagsRegister) {
            uint16_t flags = kFlagMotorAReal | kFlagLocalDevice;
            const uint32_t status = reg_read(0x64);
            const uint8_t whoami =
                static_cast<uint8_t>(reg_read(0x88) & 0xFFu);
            if (status & (1u << 0))
                flags |= kFlagSampleValid;
            if (status & (1u << 1))
                flags |= kFlagFeatureValid;
            if (status & (1u << 2))
                flags |= kFlagMpuBusy;
            if (status & (1u << 3))
                flags |= kFlagMpuError;
            if (known_mpu_id(whoami) && !(status & (1u << 3)))
                flags |= kFlagSourceReal;
            else
                flags |= kFlagSourceSim;
            if (access("/dev/soft_npu", F_OK) == 0 ||
                access("/dev/hard_npu", F_OK) == 0)
                flags |= kFlagNpuDeployed;
            if (access(
                    "/root/ai/condition/"
                    "condition_runtime_npu16_9x9_qdq_inferred.onnx",
                    F_OK) == 0)
                flags |= kFlagNpuModelReady;
            flags |= static_cast<uint16_t>(
                ((device_alert_status(slave) >> 8) & 0x3) << 11);
            return flags;
        }
        if (!local_mpu_healthy_unlocked() &&
            (index == 5 || index == 12 || index == 13) &&
            (can_override & (1u << index))) {
            return state.regs[index];
        }
        if (index < kPhysicalRegisterCount)
            return static_cast<uint16_t>(
                reg_read(kRegisters[index].pl_off) & 0xFFFF);
    } else {
        if (index == kDataFlagsRegister) {
            uint16_t flags = kFlagSampleValid | kFlagFeatureValid
                | kFlagSourceSim | kFlagLocalDevice | kFlagNpuDeployed
                | kFlagNpuModelReady;
            flags |= static_cast<uint16_t>(
                ((device_alert_status(slave) >> 8) & 0x3) << 11);
            return flags;
        }
    }
    return state.regs[index];
}

static bool write_register(uint16_t slave, uint16_t index, uint16_t value) {
    std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
    if (slave < 1 || slave > kDeviceCount ||
        index >= kRegisterCount ||
        index >= kPointSiteStatusRegister)
        return false;
    auto& state = g_devices[slave - 1];
    if (index >= kFirstSimulatedPointRegister &&
        index < kFirstSimulatedPointRegister + kSimulatedPointCount) {
        state.regs[index] = value;
        return true;
    }
    if (index == kDataFlagsRegister) {
        g_fault_site_code = static_cast<uint16_t>((value >> 11) & 0x7);
        return true;
    }
    if (index == 1 && value > 1000)
        return false;
    if (index == 0)
        value = static_cast<uint16_t>(value & 0x0007);
    if (slave == 1) {
        if (index >= kPhysicalRegisterCount ||
            !kRegisters[index].writable)
            return false;
        reg_write(kRegisters[index].pl_off, value);
        return true;
    }
    state.regs[index] = value;
    return true;
}

static bool json_string_value(
    const std::string& text,
    const std::string& key,
    std::string* value)
{
    const std::string marker = "\"" + key + "\":";
    size_t pos = text.find(marker);
    if (pos == std::string::npos)
        return false;
    pos = text.find('"', pos + marker.size());
    if (pos == std::string::npos)
        return false;
    const size_t end = text.find('"', pos + 1);
    if (end == std::string::npos)
        return false;
    *value = text.substr(pos + 1, end - pos - 1);
    return true;
}

static bool json_number_value(
    const std::string& text,
    const std::string& key,
    double* value)
{
    const std::string marker = "\"" + key + "\":";
    size_t pos = text.find(marker);
    if (pos == std::string::npos)
        return false;
    pos += marker.size();
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str() + pos, &end);
    if (end == text.c_str() + pos)
        return false;
    *value = parsed;
    return true;
}

static uint16_t condition_code(const std::string& condition)
{
    if (condition == "stopped") return 0;
    if (condition == "idle") return 1;
    if (condition == "normal") return 2;
    if (condition == "overload") return 3;
    if (condition == "fault") return 4;
    return 0xFFFF;
}

static uint16_t scaled_percent(double value)
{
    if (!std::isfinite(value))
        return 0;
    return static_cast<uint16_t>(
        std::max(0.0, std::min(10000.0, value * 10000.0)));
}

static bool run_model_for_device(uint16_t slave)
{
    if (g_can_model_override[slave - 1].load(std::memory_order_relaxed))
        return true;
    std::ostringstream values;
    values << std::fixed << std::setprecision(6);
    {
        std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
        for (int sample = 0; sample < 8; ++sample) {
            const double factor = 0.95 + sample * 0.01;
            const auto signed16 = [](uint16_t value) {
                return value & 0x8000
                    ? static_cast<int>(value) - 0x10000
                    : static_cast<int>(value);
            };
            const double rms =
                signed16(read_register(slave, 12)) / 16384.0 * 9.80665;
            const double peak =
                signed16(read_register(slave, 13)) / 16384.0 * 9.80665;
            const double temperature =
                signed16(read_register(slave, 5)) / 333.87 + 21.0;
            const double current = read_register(slave, 15) / 100.0;
            const double pwm = read_register(slave, 1) / 1000.0;
            if (sample)
                values << ',';
            values << rms * factor << ',' << peak * factor << ','
                   << temperature << ',' << current << ',' << pwm;
        }
    }

    const std::string command =
        "python3 /root/ai/condition/runtime_condition_infer.py --values \"" +
        values.str() + "\" 2>/dev/null";
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        return false;
    std::string result;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe))
        result += buffer;
    const int status = pclose(pipe);
    if (status != 0 || result.empty())
        return false;

    std::string condition;
    double confidence = 0.0;
    double latency_ms = 0.0;
    if (!json_string_value(result, "condition", &condition) ||
        !json_number_value(result, "confidence", &confidence))
        return false;
    json_number_value(result, "average_latency_ms", &latency_ms);

    auto& model = g_devices[slave - 1].model.values;
    model[0].store(condition_code(condition), std::memory_order_relaxed);
    model[1].store(scaled_percent(confidence), std::memory_order_relaxed);
    model[2].store(static_cast<uint16_t>(
        std::max(0.0, std::min(65535.0, latency_ms * 1000.0))),
        std::memory_order_relaxed);
    for (uint16_t i = 0; i < 5; ++i) {
        static constexpr const char* keys[] = {
            "stopped", "idle", "normal", "overload", "fault",
        };
        double probability = 0.0;
        json_number_value(result, keys[i], &probability);
        model[3 + i].store(
            scaled_percent(probability), std::memory_order_relaxed);
    }
    model[8].store(0x0003, std::memory_order_relaxed);
    return true;
}

static int open_can_socket()
{
    const int socket_fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socket_fd < 0)
        return -1;

    struct ifreq interface_request {};
    std::strncpy(interface_request.ifr_name, "can0",
                 sizeof(interface_request.ifr_name) - 1);
    if (ioctl(socket_fd, SIOCGIFINDEX, &interface_request) < 0) {
        close(socket_fd);
        return -1;
    }

    struct sockaddr_can address {};
    address.can_family = AF_CAN;
    address.can_ifindex = interface_request.ifr_ifindex;
    if (bind(socket_fd, reinterpret_cast<struct sockaddr*>(&address),
             sizeof(address)) < 0) {
        close(socket_fd);
        return -1;
    }

#ifdef CAN_RAW_FD_FRAMES
    const int enable_fd_frames = 1;
    setsockopt(socket_fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
               &enable_fd_frames, sizeof(enable_fd_frames));
#endif

    struct timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return socket_fd;
}

static void can_inject_worker()
{
    int socket_fd = -1;
    while (!g_stop) {
        if (socket_fd < 0) {
            socket_fd = open_can_socket();
            if (socket_fd < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
            std::puts("CAN sensor injector listening on can0");
        }

        struct canfd_frame frame {};
        const ssize_t received = read(socket_fd, &frame, sizeof(frame));
        if (received < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            close(socket_fd);
            socket_fd = -1;
            continue;
        }
        const bool valid_length =
            received == static_cast<ssize_t>(CAN_MTU)
#ifdef CANFD_MTU
            || received == static_cast<ssize_t>(CANFD_MTU)
#endif
            ;
        const uint8_t can_dlc = frame.len;
        if (!valid_length || (frame.can_id & CAN_EFF_FLAG) || can_dlc < 2) {
            continue;
        }

        const uint16_t can_id =
            static_cast<uint16_t>(frame.can_id & CAN_SFF_MASK);
        if (can_id >= 0x2A1 && can_id <= 0x2A6 && can_dlc >= 4) {
            const int model_device = can_id - 0x2A0;
            const int condition = frame.data[0];
            const uint16_t confidence = static_cast<uint16_t>(
                (static_cast<uint16_t>(frame.data[2]) << 8) | frame.data[3]);
            if (condition >= 0 && condition <= 4) {
                std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
                auto& model = g_devices[model_device - 1].model.values;
                const uint16_t target =
                    std::min<uint16_t>(confidence, 10000);
                const uint16_t remainder =
                    static_cast<uint16_t>((10000 - target) / 4);
                for (uint16_t i = 0; i < 5; ++i) {
                    model[3 + i].store(
                        i == condition ? target : remainder,
                        std::memory_order_relaxed);
                }
                model[0].store(
                    static_cast<uint16_t>(condition),
                    std::memory_order_relaxed);
                model[1].store(target, std::memory_order_relaxed);
                model[2].store(150, std::memory_order_relaxed);
                model[8].store(0x0003, std::memory_order_relaxed);
                g_can_model_override[model_device - 1].store(
                    true, std::memory_order_relaxed);
            }
            continue;
        }
        if (can_id >= 0x2B1 && can_id <= 0x2B6 &&
            can_dlc >= 1 && frame.data[0] == 0xFF) {
            const int model_device = can_id - 0x2B0;
            g_can_model_override[model_device - 1].store(
                false, std::memory_order_relaxed);
            g_devices[model_device - 1].model.values[8].store(
                0, std::memory_order_relaxed);
            continue;
        }
        int device_id = 0;
        enum class Payload {
            TemperatureCurrent,
            RmsPeak,
            Points12,
            Points34,
            Point5,
        } payload;
        if (can_id >= 0x241 && can_id <= 0x246) {
            device_id = can_id - 0x240;
            payload = Payload::TemperatureCurrent;
        } else if (can_id >= 0x251 && can_id <= 0x256) {
            device_id = can_id - 0x250;
            payload = Payload::RmsPeak;
        } else if (can_id >= 0x261 && can_id <= 0x266) {
            device_id = can_id - 0x260;
            payload = Payload::Points12;
        } else if (can_id >= 0x271 && can_id <= 0x276) {
            device_id = can_id - 0x270;
            payload = Payload::Points34;
        } else if (can_id >= 0x281 && can_id <= 0x286) {
            device_id = can_id - 0x280;
            payload = Payload::Point5;
        } else {
            continue;
        }
        if (device_id < 1 || device_id > kDeviceCount)
            continue;

        auto read_be16 = [](const uint8_t* bytes) {
            return static_cast<uint16_t>(
                (static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
        };
        {
            std::lock_guard<std::recursive_mutex> lock(g_state_mutex);
            auto& registers = g_devices[device_id - 1].regs;
            auto& override_mask = g_can_override_mask[device_id - 1];
            if (can_dlc < 4)
                continue;
            if (payload == Payload::TemperatureCurrent) {
                registers[5] = read_be16(&frame.data[0]);
                registers[15] = read_be16(&frame.data[2]);
                override_mask.fetch_or(
                    static_cast<uint32_t>((1u << 5) | (1u << 15)));
            } else if (payload == Payload::RmsPeak) {
                registers[12] = read_be16(&frame.data[0]);
                registers[13] = read_be16(&frame.data[2]);
                override_mask.fetch_or(
                    static_cast<uint32_t>((1u << 12) | (1u << 13)));
            } else if (payload == Payload::Points12) {
                registers[16] = read_be16(&frame.data[0]);
                registers[17] = read_be16(&frame.data[2]);
                override_mask.fetch_or(
                    static_cast<uint32_t>((1u << 16) | (1u << 17)));
            } else if (payload == Payload::Points34) {
                registers[18] = read_be16(&frame.data[0]);
                registers[19] = read_be16(&frame.data[2]);
                override_mask.fetch_or(
                    static_cast<uint32_t>((1u << 18) | (1u << 19)));
            } else if (payload == Payload::Point5) {
                registers[20] = read_be16(&frame.data[0]);
                override_mask.fetch_or(static_cast<uint32_t>(1u << 20));
// Point5 marks a complete state update. Re-run that device immediately
// instead of waiting for the ten-second full sweep.
                g_model_refresh_mask.fetch_or(
                    static_cast<uint32_t>(1u << (device_id - 1)));
            }
        }
    }

    if (socket_fd >= 0)
        close(socket_fd);
}

static void model_worker()
{
    auto next_full_sweep = std::chrono::steady_clock::now() +
                           std::chrono::seconds(2);
    while (!g_stop) {
        const auto now = std::chrono::steady_clock::now();
        const bool full_sweep = now >= next_full_sweep;
        const uint32_t requested =
            g_model_refresh_mask.exchange(0, std::memory_order_relaxed);
        for (uint16_t slave = 1; slave <= kDeviceCount && !g_stop; ++slave) {
            const uint32_t bit =
                static_cast<uint32_t>(1u << (slave - 1));
            if (!full_sweep && (requested & bit) == 0)
                continue;
            if (!run_model_for_device(slave))
                g_devices[slave - 1].model.values[8].store(
                    0, std::memory_order_relaxed);
        }
        if (full_sweep)
            next_full_sweep = now + std::chrono::seconds(10);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static void log_csv(std::ofstream& csv) {
    if (!csv.is_open())
        return;
    uint16_t ctrl = read_register(1, 0);
    uint16_t pwm = read_register(1, 1);
    uint16_t flags = read_register(1, kDataFlagsRegister);
    uint16_t rms = read_register(1, 12);
    uint16_t peak = read_register(1, 13);
    uint16_t current = read_register(1, kCurrentRegister);
    const uint16_t low = read_register(1, 10);
    const uint16_t high = read_register(1, 11);
    unsigned alert = rms >= high ? 2 : rms >= low ? 1 : 0;
    csv << now_ms() << ',' << ctrl << ',' << pwm << ',' << flags << ','
        << rms << ',' << peak << ',' << current << ',' << alert << '\n';
    csv.flush();
}

static void usage(const char* prog) {
    std::fprintf(stderr,
                 "Usage: %s [--csv /root/serial_hmi_log.csv]\n"
                 "       %s --no-csv\n"
                 "       Logical Modbus slave IDs 1..6 are always enabled.\n"
                 "PL_REG_BASE env default 0x80000000\n",
                 prog, prog);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);
    std::signal(SIGHUP, on_sigint);

    uint8_t slave_id = 1;
    std::string csv_path = "/root/serial_hmi_log.csv";
    bool csv_enabled = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--slave") == 0 && i + 1 < argc) {
            slave_id = static_cast<uint8_t>(parse_u32(argv[++i], "slave"));
        } else if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
            csv_path = argv[++i];
            csv_enabled = true;
        } else if (std::strcmp(argv[i], "--no-csv") == 0) {
            csv_enabled = false;
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    const uint32_t base_phys = []() {
        const char* env = std::getenv("PL_REG_BASE");
        return env ? parse_u32(env, "PL_REG_BASE") : 0x80000000u;
    }();

    g_regs = map_pl_base(base_phys);
    rs485_init();
    rs485_flush_input();
    initialize_devices();
    // Slave ID 0 enables the PL parser's ID1..6 logical-node filter.
    configure_parser(0, kRegisterCount);
    acknowledge_stale_frames();

    std::ofstream csv;
    if (csv_enabled) {
        csv.open(csv_path, std::ios::app);
        if (!csv.is_open()) {
            std::fprintf(stderr, "warn: cannot open CSV %s\n", csv_path.c_str());
        } else {
            csv.seekp(0, std::ios::end);
            if (csv.tellp() == 0)
                csv << "timestamp_ms,motor_ctrl,motor_pwm,data_flags,rms,peak,alert\n";
        }
    }

    std::printf("Modbus RTU multi-node slave: ids=1..%u, 9600 8N1, "
                "registers/device=0..%u",
                static_cast<unsigned>(kDeviceCount),
                static_cast<unsigned>(kRegisterCount - 1));
    if (csv_enabled)
        std::printf(", csv=%s", csv_path.c_str());
    std::printf("\n");
    std::fflush(stdout);

    (void)slave_id;
    std::thread model_thread;
    if (access("/root/ai/condition/runtime_condition_infer.py", F_OK) == 0)
        model_thread = std::thread(model_worker);
    std::thread can_thread(can_inject_worker);
    prioritize_serial_thread();

    auto last_log = std::chrono::steady_clock::now();
    while (!g_stop) {
        uint8_t request[8] = {};
        if (read_request(request, 250)) {
            if (!frame_crc_ok(request, sizeof(request)))
                continue;

            const uint8_t req_slave = request[0];
            const uint8_t function = request[1];
            if (req_slave < 1 || req_slave > kDeviceCount)
                continue;

            if (function == 0x03) {
                const uint16_t start =
                    static_cast<uint16_t>((request[2] << 8) | request[3]);
                const uint16_t count =
                    static_cast<uint16_t>((request[4] << 8) | request[5]);
                if (count < 1 || count > 125 ||
                    static_cast<uint32_t>(start) + count > kRegisterCount) {
                    std::vector<uint8_t> response;
                    make_exception(req_slave, function, 0x02, response);
                    rs485_tx(response.data(), response.size());
                    continue;
                }

                std::vector<uint8_t> response = {
                    req_slave,
                    0x03,
                    static_cast<uint8_t>(count * 2),
                };
                for (uint16_t i = 0; i < count; ++i) {
                    uint16_t value = read_register(
                        req_slave, static_cast<uint16_t>(start + i));
                    response.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
                    response.push_back(static_cast<uint8_t>(value & 0xFF));
                }
                append_crc(response);
                rs485_tx(response.data(), response.size());
                std::printf("[FC03] start=%u count=%u\n", start, count);
                std::fflush(stdout);
            } else if (function == 0x06) {
                const uint16_t index =
                    static_cast<uint16_t>((request[2] << 8) | request[3]);
                const uint16_t value =
                    static_cast<uint16_t>((request[4] << 8) | request[5]);
                if (!write_register(req_slave, index, value)) {
                    std::vector<uint8_t> response;
                    make_exception(req_slave, function, 0x02, response);
                    rs485_tx(response.data(), response.size());
                    continue;
                }

                std::vector<uint8_t> response(request, request + sizeof(request));
                rs485_tx(response.data(), response.size());
                std::printf("[FC06] reg=%u value=%u\n", index, value);
                std::fflush(stdout);
            } else {
                std::vector<uint8_t> response;
                make_exception(req_slave, function, 0x01, response);
                rs485_tx(response.data(), response.size());
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (csv.is_open() &&
            std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() >= 1) {
            log_csv(csv);
            last_log = now;
        }
    }

    if (model_thread.joinable())
        model_thread.join();
    if (can_thread.joinable())
        can_thread.join();
    std::puts("Modbus RTU slave stopped");
    return 0;
}
