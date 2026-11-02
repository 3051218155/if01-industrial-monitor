/*
 * gateway.cpp — IF-01 边缘网关 TCP 服务（真实数据版）
 *
 * 监听 0.0.0.0:9000，接收一行 JSON 请求，返回一行 JSON 应答。
 * 数据来自 PL 的 AXI-Lite 寄存器（MPU6050 -> 去直流 -> FIR -> RMS/PEAK）。
 *
 * 请求示例：
 *   {"cmd":"ping"}
 *   {"cmd":"query_device","device_id":1..6}
 *   {"cmd":"list_devices"}
 *
 * 编译（板端）：
 *   g++ -O2 -Wall -o gateway gateway.cpp
 * 运行：
 *   PL_REG_BASE=0x80000000 ./gateway 9000
 */

#include <arpa/inet.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr size_t kMapSize = 0x1000;
constexpr off_t  kOffVersion   = 0x30;
constexpr off_t  kOffSensorCtrl = 0x60;  // bit0 enable, bit1 1=MPU
constexpr off_t  kOffStatus    = 0x64;   // bit5:4 = alert_level
constexpr off_t  kOffRaw       = 0x68;
constexpr off_t  kOffFiltered  = 0x6C;
constexpr off_t  kOffRms       = 0x70;
constexpr off_t  kOffPeak      = 0x74;
constexpr off_t  kOffWhoami    = 0x88;
constexpr off_t  kOffAlertLow  = 0x80;
constexpr off_t  kOffAlertHigh = 0x84;

// 本地 MPU 的标定阈值（恢复到"自动"时用）
constexpr uint32_t kEnvAlertLow  = 3000;
constexpr uint32_t kEnvAlertHigh = 4500;
constexpr const char *kSnapshotDir = "/root/fault_snapshots";
constexpr int kSnapshotPre = 64;
constexpr int kSnapshotPost = 128;

bool knownMpuId(uint8_t id) {
    switch (id) {
        case 0x68: case 0x70: case 0x71: case 0x72:
        case 0x73: case 0x74: case 0x75: case 0x98:
            return true;
        default:
            return false;
    }
}

// PL RS485 寄存器（IF-01 v0.2）
constexpr off_t  kOffRsTx    = 0x50;
constexpr off_t  kOffRsRx    = 0x54;
constexpr off_t  kOffRsStat  = 0x58;
constexpr off_t  kOffRsCtrl  = 0x5C;
constexpr uint32_t kStatTxBusy  = 1u << 0;
constexpr uint32_t kStatRxValid = 1u << 1;
constexpr uint32_t kCtrlAutoDe  = 1u << 1;
constexpr uint32_t kCtrlRxEn    = 1u << 2;
constexpr uint32_t kCtrlTxEn    = 1u << 3;

constexpr double kLsbPerG   = 16384.0;   // MPU-6050 默认 +-2g
constexpr double kGToMs2    = 9.80665;

// 从站上报的故障部位编码
const char *siteNameFromCode(int code) {
    switch (code) {
        case 1: return "轴承端";
        case 2: return "转子不平衡";
        case 3: return "地脚松动";
        case 4: return "联轴器";
        case 5: return "机壳";
        default: return "无";
    }
}

const char *siteKeyFromCode(int code) {
    switch (code) {
        case 1: return "bearing";
        case 2: return "imbalance";
        case 3: return "looseness";
        case 4: return "coupling";
        case 5: return "body";
        default: return "none";
    }
}

volatile uint32_t *g_regs = nullptr;
volatile sig_atomic_t g_stop = 0;

void on_sigint(int) { g_stop = 1; }

volatile uint32_t *mapPl(uint32_t base_phys) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { std::perror("open /dev/mem (need root)"); std::exit(1); }
    void *m = mmap(nullptr, kMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                   static_cast<off_t>(base_phys));
    close(fd);
    if (m == MAP_FAILED) { std::perror("mmap"); std::exit(1); }
    return reinterpret_cast<volatile uint32_t *>(m);
}

inline uint32_t rd(off_t off) { return g_regs[off / 4]; }
inline void wr(off_t off, uint32_t v) { g_regs[off / 4] = v; }

// ---------------- RS485 Modbus RTU 主站（FC03）----------------

uint16_t crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= d[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
                            : static_cast<uint16_t>(crc >> 1);
    }
    return crc;
}

void rs485Init() { wr(kOffRsCtrl, kCtrlAutoDe | kCtrlRxEn | kCtrlTxEn); }

bool rsTxByte(uint8_t b) {
    for (int i = 0; i < 3000; ++i) {
        if ((rd(kOffRsStat) & kStatTxBusy) == 0) break;
        usleep(100);
    }
    wr(kOffRsTx, b);
    for (int i = 0; i < 3000; ++i) {
        if ((rd(kOffRsStat) & kStatTxBusy) == 0) return true;
        usleep(100);
    }
    return false;
}

bool rsRxByte(uint8_t *out, int timeout_ms) {
    for (int i = 0; i < timeout_ms * 10; ++i) {
        if (rd(kOffRsStat) & kStatRxValid) {
            *out = static_cast<uint8_t>(rd(kOffRsRx) & 0xFF);
            return true;
        }
        usleep(100);
    }
    return false;
}

void rsFlush() {
    for (int i = 0; i < 256; ++i) {
        if (!(rd(kOffRsStat) & kStatRxValid)) break;
        (void)rd(kOffRsRx);
    }
}

// 读从站保持寄存器；成功返回 true
bool modbusRead03(uint8_t slave, uint16_t start, uint16_t count, uint16_t *out) {
    uint8_t req[8] = {slave, 0x03,
                      static_cast<uint8_t>(start >> 8), static_cast<uint8_t>(start & 0xFF),
                      static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count & 0xFF),
                      0, 0};
    const uint16_t c = crc16(req, 6);
    req[6] = static_cast<uint8_t>(c & 0xFF);
    req[7] = static_cast<uint8_t>(c >> 8);

    rsFlush();
    for (uint8_t b : req)
        if (!rsTxByte(b)) return false;

    usleep(4000);
    uint8_t resp[64];
    size_t n = 0;
    int budget = 120;
    while (budget > 0 && n < sizeof(resp)) {
        uint8_t b;
        if (!rsRxByte(&b, 20)) {
            budget -= 20;
            if (n > 0 && budget < 60) break;
            continue;
        }
        resp[n++] = b;
        budget = 60;
    }
    if (n < 5) return false;
    const uint16_t cc = crc16(resp, n - 2);
    const uint16_t rc = static_cast<uint16_t>(resp[n - 2]) | (static_cast<uint16_t>(resp[n - 1]) << 8);
    if (cc != rc) return false;
    if (resp[0] != slave || resp[1] != 0x03) return false;
    const uint8_t bc = resp[2];
    if (n != static_cast<size_t>(3 + bc + 2)) return false;
    for (unsigned i = 0; i + 1 < bc; i += 2)
        out[i / 2] = static_cast<uint16_t>((resp[3 + i] << 8) | resp[3 + i + 1]);
    return true;
}

// ---- 板端"多测点比较"定位算法 ----
struct BusResult {
    bool ok = false;
    bool has_vib = false;     // 是否从 Modbus 读到了模拟振动 RMS/峰值
    int site_code = 0;        // 0 无, 1 轴承, 2 不平衡, 3 松动, 4 联轴器, 5 机壳
    int site_max = 0;         // 最大测点值（×100）
    int over_count = 0;       // 超阈值测点数
    int point[5] = {0, 0, 0, 0, 0};
    int temp_x10 = 0;         // 从站上报温度 ×10
    int current_x100 = 0;     // 从站上报电流 ×100
    int rms_x100 = 0;         // 模拟振动 RMS，m/s² ×100
    int peak_x100 = 0;        // 模拟振动峰值，m/s² ×100
    int alert_level = -1;     // 从站显式上报等级 0/1/2，-1 表示未提供
    bool has_env = false;     // 温度/电流寄存器是否存在
};

// 阈值：测点振动 RMS ×100，超过即判为该点异常
constexpr int kPointWarn = 250;   // 2.50
constexpr int kPointFault = 600;  // 6.00
// 从站上报温度的阈值（×10）
constexpr int kTempWarn = 700;    // 70.0 C
constexpr int kTempFault = 850;   // 85.0 C

BusResult busAnalyze(uint8_t slave) {
    BusResult r;
    uint16_t regs[10] = {0};
    const bool full = modbusRead03(slave, 0, 10, regs);
    if (!full && modbusRead03(slave, 0, 7, regs)) {
        r.has_env = true;
    } else if (!full) {
        // 从站只配了 5 个测点时，退回只读测点。
        uint16_t five[10] = {0};
        if (!modbusRead03(slave, 0, 5, five)) return r;
        for (int i = 0; i < 5; ++i) regs[i] = five[i];
    } else {
        r.has_env = true;
        r.has_vib = true;
    }
    r.ok = true;
    int best = 0, bestIdx = -1;
    for (int i = 0; i < 5; ++i) {
        r.point[i] = regs[i];
        if (regs[i] >= kPointWarn) ++r.over_count;
        if (regs[i] > best) { best = regs[i]; bestIdx = i; }
    }
    r.site_max = best;
    r.temp_x10 = regs[5];
    r.current_x100 = regs[6];
    if (full) {
        r.rms_x100 = regs[7];
        r.peak_x100 = regs[8];
        const int level = (regs[9] >> 8) & 0xFF;
        const int site = regs[9] & 0xFF;
        if (level <= 2) r.alert_level = level;
        if (site >= 1 && site <= 5)
            r.site_code = site;
    }
    if (r.site_code == 0 && best >= kPointWarn && bestIdx >= 0)
        r.site_code = bestIdx + 1;
    return r;
}

constexpr int kDeviceCount = 6;
constexpr int kPolledDeviceCount = kDeviceCount;
std::array<BusResult, kDeviceCount> g_bus;  // 每台电机独立的 Modbus 分析结果
std::array<uint64_t, kDeviceCount> g_busLastMs{};  // 每台设备上次轮询时间
int g_forceLevel = -1;    // >=0 时强制告警等级（-1 = 自动）
int g_appliedLevel = -1;  // 已经写进 PL 的等级，避免重复写
bool g_localSensorValid = true;
bool g_mpuBusyWasHigh = false;
uint64_t g_mpuBusySinceMs = 0;
uint64_t g_lastSensorResetMs = 0;
std::array<int, kDeviceCount> g_lastSnapshotLevel{};

uint64_t nowMs();

bool directMultiNodeMode() {
    static const bool enabled = [] {
        const char *value = std::getenv("FPGA_GATEWAY_DIRECT_MULTINODE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void refreshBus(int id) {
    // The board-side multi-node slave owns the RS485 modem. The gateway must
    // not issue a second set of master requests on the same UART.
    if (directMultiNodeMode())
        return;
    if (id < 1 || id > kPolledDeviceCount)
        return;
    const int idx = id - 1;
    const uint64_t now = nowMs();
    const uint64_t interval = g_bus[idx].ok ? 1000 : 5000;
    if (now - g_busLastMs[idx] < interval)
        return;
    if (!g_bus[idx].ok) {
        uint16_t probe = 0;
        if (!modbusRead03(static_cast<uint8_t>(id), 0, 1, &probe)) {
            g_busLastMs[idx] = now;
            return;
        }
    }
    g_bus[idx] = busAnalyze(static_cast<uint8_t>(id));
    g_busLastMs[idx] = now;
}

// 把"有效告警等级"落到 PL 的阈值寄存器上，
// 这样物理三色灯 + 蜂鸣器 就和 3D 显示同步了。
// 自动档用可恢复阈值，不再把 0/0 写死成自锁严重档。
void applyAlertLevel(int level) {
    if (level == g_appliedLevel) return;
    g_appliedLevel = level;

    if (g_forceLevel < 0 && !g_localSensorValid) {
        wr(kOffAlertLow, 0xFFFFFFFFu);
        wr(kOffAlertHigh, 0xFFFFFFFFu);
        return;
    }

    if (level == 2) {
        wr(kOffAlertLow, 0);
        wr(kOffAlertHigh, 0);
    } else if (level == 1) {
        wr(kOffAlertLow, 0);
        wr(kOffAlertHigh, 0xFFFFFFFFu);
    } else {
        wr(kOffAlertLow, 0xFFFFFFFFu);
        wr(kOffAlertHigh, 0xFFFFFFFFu);
    }
}

// ---- 极简 JSON 取值（只处理本协议用的平坦字段）----
std::string jsonStr(const std::string &body, const std::string &key) {
    std::string pat = "\"" + key + "\"";
    size_t p = body.find(pat);
    if (p == std::string::npos) return {};
    p = body.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    ++p;
    while (p < body.size() && (body[p] == ' ' || body[p] == '\t')) ++p;
    if (p < body.size() && body[p] == '"') {
        size_t e = body.find('"', p + 1);
        if (e == std::string::npos) return {};
        return body.substr(p + 1, e - p - 1);
    }
    size_t e = p;
    while (e < body.size() && body[e] != ',' && body[e] != '}') ++e;
    return body.substr(p, e - p);
}

int jsonInt(const std::string &body, const std::string &key, int defv) {
    std::string s = jsonStr(body, key);
    if (s.empty()) return defv;
    try { return std::stoi(s); } catch (...) { return defv; }
}

std::string nowString() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string snapshotTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t time = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d_%H%M%S")
        << '_' << std::setw(3) << std::setfill('0') << ms.count();
    return out.str();
}

bool ensureSnapshotDir() {
    if (mkdir(kSnapshotDir, 0755) == 0)
        return true;
    return errno == EEXIST;
}

std::string captureFaultSnapshot(
    int device_id,
    int level,
    double rms_ms2,
    double peak_ms2,
    int site_code,
    int current_raw
) {
    if (!ensureSnapshotDir())
        return {};

    const std::string stamp = snapshotTimestamp();
    const std::string stem = std::string(kSnapshotDir) + "/fault_" + stamp +
                             "_dev" + std::to_string(device_id);
    const std::string csv_path = stem + ".csv";
    const std::string json_path = stem + ".json";
    const bool simulated = device_id > 1;

    std::ofstream csv(csv_path, std::ios::out | std::ios::trunc);
    if (!csv)
        return {};
    csv << "sequence,relative_us,sample_lsb,sample_ms2,alert_level,source\n";

    const int total = kSnapshotPre + kSnapshotPost;
    const double amplitude_lsb = rms_ms2 * kLsbPerG / kGToMs2;
    for (int i = 0; i < total; ++i) {
        const int relative = i - kSnapshotPre;
        int32_t sample = 0;
        if (simulated) {
            sample = static_cast<int32_t>(
                amplitude_lsb *
                std::sin(2.0 * 3.141592653589793 * relative / 24.0)
            );
        } else if (relative >= 0) {
            sample = static_cast<int32_t>(rd(kOffRaw));
            usleep(2000);
        } else {
            sample = current_raw;
        }
        const double sample_ms2 =
            static_cast<double>(sample) / kLsbPerG * kGToMs2;
        csv << i << ',' << relative * 2000 << ',' << sample << ','
            << std::fixed << std::setprecision(6) << sample_ms2 << ','
            << level << ',' << (simulated ? "modbus_simulation" : "pl_register")
            << '\n';
    }
    csv.flush();

    std::ofstream meta(json_path, std::ios::out | std::ios::trunc);
    if (meta) {
        meta << "{\n"
             << "  \"device_id\": " << device_id << ",\n"
             << "  \"timestamp\": \"" << nowString() << "\",\n"
             << "  \"alert_level\": " << level << ",\n"
             << "  \"fault_site_code\": " << site_code << ",\n"
             << "  \"vibration_rms_ms2\": " << std::fixed
             << std::setprecision(6) << rms_ms2 << ",\n"
             << "  \"vibration_peak_ms2\": " << peak_ms2 << ",\n"
             << "  \"sample_rate_hz\": 500,\n"
             << "  \"trigger_index\": " << kSnapshotPre << ",\n"
             << "  \"pre_samples\": " << kSnapshotPre << ",\n"
             << "  \"post_samples\": " << kSnapshotPost << ",\n"
             << "  \"simulated\": " << (simulated ? "true" : "false") << ",\n"
             << "  \"csv\": \"" << csv_path << "\"\n"
             << "}\n";
    }
    return csv_path;
}

std::string updateFaultSnapshot(
    int device_id,
    int level,
    double rms_ms2,
    double peak_ms2,
    int site_code,
    int current_raw
) {
    const int idx = std::max(1, std::min(device_id, kDeviceCount)) - 1;
    if (level <= 0) {
        g_lastSnapshotLevel[idx] = 0;
        return {};
    }
    if (level <= g_lastSnapshotLevel[idx])
        return {};
    const std::string path = captureFaultSnapshot(
        device_id,
        level,
        rms_ms2,
        peak_ms2,
        site_code,
        current_raw
    );
    g_lastSnapshotLevel[idx] = level;
    return path;
}

int busLevelOf(const BusResult &bus) {
    if (!bus.ok)
        return 0;

    int level = 0;
    if (bus.alert_level >= 0) {
        level = bus.alert_level;
    } else if (bus.has_vib) {
        level = (bus.rms_x100 >= kPointFault) ? 2
              : (bus.rms_x100 >= kPointWarn) ? 1 : 0;
    }

    if (bus.site_code != 0 && bus.site_max >= kPointFault)
        level = std::max(level, 2);
    else if (bus.site_code != 0 && bus.site_max >= kPointWarn)
        level = std::max(level, 1);

    if (bus.has_env) {
        const int tempLevel = (bus.temp_x10 >= kTempFault) ? 2
                            : (bus.temp_x10 >= kTempWarn) ? 1 : 0;
        level = std::max(level, tempLevel);
    }
    return level;
}

std::string snapJson(int device_id) {
    refreshBus(device_id);
    const int idx = std::max(1, std::min(device_id, kDeviceCount)) - 1;
    const BusResult &bus = g_bus[idx];

    uint32_t status = 0;
    uint32_t rms = 0;
    uint32_t peak = 0;
    uint32_t raw = 0;
    uint32_t filt = 0;
    uint32_t who = 0;
    int level = busLevelOf(bus);

    // 当前只有 1 号机接了真实 MPU。2..6 号完全使用 Modbus 模拟值。
    const bool localMpu = (device_id == 1);
    const uint64_t nowTs = nowMs();
    if (localMpu) {
        status = rd(kOffStatus);
        rms = rd(kOffRms);
        peak = rd(kOffPeak);
        raw = rd(kOffRaw);
        filt = rd(kOffFiltered);
        who = rd(kOffWhoami) & 0xFF;

        const bool mpuBusyNow = (status & 0x4) != 0;
        const bool ctrlMpu = (rd(kOffSensorCtrl) & 0x3) == 0x3;
        if (mpuBusyNow && ctrlMpu) {
            if (!g_mpuBusyWasHigh)
                g_mpuBusySinceMs = nowTs;
        } else {
            g_mpuBusySinceMs = 0;
        }
        g_mpuBusyWasHigh = mpuBusyNow;

        const bool mpuStuck = mpuBusyNow && ctrlMpu &&
                              nowTs - g_mpuBusySinceMs > 5000;
        if (!knownMpuId(static_cast<uint8_t>(who)) || mpuStuck) {
            if (nowTs - g_lastSensorResetMs > 5000) {
                g_lastSensorResetMs = nowTs;
                wr(kOffSensorCtrl, 0x1);   // 切断 MPU，复位 reader FSM
                usleep(150000);
                wr(kOffSensorCtrl, 0x3);   // 重新切回 MPU
                status = rd(kOffStatus);
                who = rd(kOffWhoami) & 0xFF;
                g_mpuBusySinceMs = 0;
                g_mpuBusyWasHigh = false;
            }
        }
    }

    const bool mpuError = localMpu && ((status & 0x8) != 0);
    const bool localMpuOk =
        localMpu && knownMpuId(static_cast<uint8_t>(who)) && !mpuError;
    g_localSensorValid = localMpu ? localMpuOk : true;
    if (localMpu) {
        // 用真实 RMS 判级，不再引用 PL 已锁定的 alert_level，避免自锁
        const int localLevel = (rms >= kEnvAlertHigh) ? 2 : (rms >= kEnvAlertLow) ? 1 : 0;
        if (localLevel > level)
            level = localLevel;
    }

    if ((device_id > 1 && !bus.ok) || (device_id == 1 && !localMpuOk && !bus.ok)) {
        applyAlertLevel(0);
        return "{\"code\":1,\"msg\":\"device offline\",\"data\":{\"device_id\":" +
               std::to_string(device_id) + "}}";
    }
    double rms_ms2 = 0.0;
    double peak_ms2 = 0.0;
    if (localMpuOk) {
        rms_ms2 = static_cast<double>(rms) / kLsbPerG * kGToMs2;
        peak_ms2 = static_cast<double>(peak) / kLsbPerG * kGToMs2;
    } else if (bus.ok && bus.has_vib) {
        rms_ms2 = bus.rms_x100 / 100.0;
        peak_ms2 = bus.peak_x100 / 100.0;
        if (peak_ms2 <= 0.0)
            peak_ms2 = rms_ms2 * 1.5;
    } else if (bus.ok && bus.site_max > 0) {
        // 兼容只有 5 测点寄存器的旧从站：最大测点作为模拟 RMS。
        rms_ms2 = bus.site_max / 100.0;
        peak_ms2 = rms_ms2 * 1.5;
    }

    int siteCode = (level == 0) ? 0 : bus.site_code;
    // 单 MPU 拿不到 Modbus 测点部位时，默认映射到机壳，3D 才有对应高亮
    if (siteCode == 0 && level > 0 && localMpuOk)
        siteCode = 5;   // body / 机壳
    const char *site = siteNameFromCode(siteCode);
    const char *skey = siteKeyFromCode(siteCode);
    const char *ftype = (level == 0) ? "无"
                      : (siteCode ? (localMpuOk ? "测点振动超限" : "通道振动超限")
                                  : "整体振动异常");

    const char *st = (level == 0) ? "normal" : (level == 1) ? "fault_minor" : "fault_major";
    const char *stext = (level == 0) ? "运行正常" : (level == 1) ? "轻微故障" : "严重故障";
    std::string source = localMpuOk
        ? (bus.ok ? "MPU6050 + PL + 辅助通道"
                  : "MPU6050 + PL")
        : ("通道 " + std::to_string(device_id));
    std::string algo = localMpuOk
        ? "MPU6050 + PL 融合"
        : ("通道 " + std::to_string(device_id) + " 特征");
    const std::string fault_snapshot = updateFaultSnapshot(
        device_id,
        level,
        rms_ms2,
        peak_ms2,
        siteCode,
        static_cast<int32_t>(raw)
    );

    int systemLevel = level;
    for (const BusResult &item : g_bus)
        systemLevel = std::max(systemLevel, busLevelOf(item));
    if (g_forceLevel >= 0)
        systemLevel = g_forceLevel;
    applyAlertLevel(systemLevel);
    if (systemLevel == 0)
        usleep(2000);

    char buf[1200];
    std::snprintf(buf, sizeof(buf),
        "{\"code\":0,\"msg\":\"ok\",\"data\":{"
        "\"device_id\":%d,"
        "\"device_name\":\"%d号电机\","
        "\"status\":\"%s\","
        "\"status_text\":\"%s\","
        "\"alert_level\":%d,"
        "\"vibration_rms\":%.4f,"
        "\"vibration_peak\":%.4f,"
        "\"rms_lsb\":%u,"
        "\"peak_lsb\":%u,"
        "\"raw_lsb\":%d,"
        "\"filtered_lsb\":%d,"
        "\"mpu_whoami\":%u,"
        "\"fault_location\":\"%s\","
        "\"fault_type\":\"%s\","
        "\"fault_site\":\"%s\","
        "\"bus_ok\":%s,"
        "\"bus_has_vib\":%s,"
        "\"bus_point1\":%d,\"bus_point2\":%d,\"bus_point3\":%d,\"bus_point4\":%d,\"bus_point5\":%d,"
        "\"bus_site_code\":%d,"
        "\"bus_has_env\":%s,"
        "\"temperature\":%.1f,"
        "\"current\":%.2f,"
        "\"simulated\":%s,"
        "\"algo\":\"%s\","
        "\"source\":\"%s\","
        "\"fault_snapshot\":\"%s\","
        "\"timestamp\":\"%s\"}}",
        device_id, device_id, st, stext, level,
        rms_ms2, peak_ms2, rms, peak,
        static_cast<int32_t>(raw), static_cast<int32_t>(filt),
        who, site, ftype, skey,
        bus.ok ? "true" : "false",
        bus.has_vib ? "true" : "false",
        bus.point[0], bus.point[1], bus.point[2], bus.point[3], bus.point[4],
        bus.site_code,
        bus.has_env ? "true" : "false",
        bus.temp_x10 / 10.0,
        bus.current_x100 / 100.0,
        localMpuOk ? "false" : "true",
        algo.c_str(),
        source.c_str(),
        fault_snapshot.c_str(),
        nowString().c_str());
    return buf;
}

std::string handle(const std::string &body) {
    std::string cmd = jsonStr(body, "cmd");
    if (cmd.empty()) cmd = jsonStr(body, "command");

    // 请求日志（排查"Qt 有没有在轮询"）
    if (std::FILE *f = std::fopen("/root/gateway_req.log", "a")) {
        std::fprintf(f, "%llu %s\n",
                     static_cast<unsigned long long>(nowMs()), cmd.c_str());
        std::fclose(f);
    }

    if (cmd == "ping")   return "{\"code\":0,\"msg\":\"ok\",\"data\":{\"gateway\":\"if01\",\"online\":true}}";
    if (cmd == "npu_condition_live") {
        const std::string values = jsonStr(body, "values");
        if (values.empty() || values.size() > 1024)
            return "{\"code\":1,\"msg\":\"missing live values\",\"data\":{}}";
        const bool safe = std::all_of(
            values.begin(), values.end(),
            [](char value) {
                return (value >= '0' && value <= '9') || value == ',' ||
                       value == '.' || value == '-' || value == '+' ||
                       value == 'e' || value == 'E';
            });
        if (!safe)
            return "{\"code\":1,\"msg\":\"invalid live values\",\"data\":{}}";

        const std::string command =
            "python3 /root/ai/condition/runtime_condition_infer.py --values \"" +
            values + "\" 2>/dev/null";
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe)
            return "{\"code\":1,\"msg\":\"runtime npu command failed\",\"data\":{}}";
        std::string result;
        char buffer[1024];
        while (std::fgets(buffer, sizeof(buffer), pipe))
            result += buffer;
        const int status = pclose(pipe);
        if (status != 0 || result.empty())
            return "{\"code\":1,\"msg\":\"runtime npu inference failed\",\"data\":{}}";
        return "{\"code\":0,\"msg\":\"ok\",\"data\":{\"result\":" + result + "}}";
    }
    if (cmd == "npu_condition") {
        std::string sample = jsonStr(body, "sample");
        if (sample.empty())
            sample = "normal";
        const std::array<const char *, 5> samples = {
            "stopped", "idle", "normal", "overload", "fault",
        };
        if (std::find(samples.begin(), samples.end(), sample) == samples.end())
            return "{\"code\":1,\"msg\":\"invalid sample\",\"data\":{}}";

        static std::string cachedSample;
        static std::string cachedResult;
        std::string result;
        if (sample == cachedSample && !cachedResult.empty()) {
            result = cachedResult;
        } else {
            const std::string command =
                "python3 /root/ai/condition/npu_condition_infer.py --samples "
                "/root/ai/condition/condition_npu_samples/" + sample +
                ".json 2>/dev/null";
            FILE *pipe = popen(command.c_str(), "r");
            if (!pipe)
                return "{\"code\":1,\"msg\":\"npu command failed\",\"data\":{}}";
            char buffer[512];
            while (std::fgets(buffer, sizeof(buffer), pipe))
                result += buffer;
            const int status = pclose(pipe);
            if (status != 0 || result.empty())
                return "{\"code\":1,\"msg\":\"npu inference failed\",\"data\":{}}";
            cachedSample = sample;
            cachedResult = result;
        }
        return "{\"code\":0,\"msg\":\"ok\",\"data\":{\"sample\":\"" + sample +
               "\",\"result\":" + result + "}}";
    }
    if (cmd == "force_alert") {
        // 硬件联动自检：直接强制告警等级，看板子上的三色灯/蜂鸣器
        //   {"cmd":"force_alert","level":2}  -> 红灯 + 蜂鸣器
        //   {"cmd":"force_alert","level":1}  -> 蓝灯
        //   {"cmd":"force_alert","level":-1} -> 恢复自动（由 MPU/总线决定）
        int lv = jsonInt(body, "level", -1);
        g_forceLevel = (lv < 0 || lv > 2) ? -1 : lv;
        applyAlertLevel(g_forceLevel >= 0 ? g_forceLevel : 0);
        char b[128];
        std::snprintf(b, sizeof(b),
            "{\"code\":0,\"msg\":\"ok\",\"data\":{\"force_level\":%d}}", g_forceLevel);
        return b;
    }
    if (cmd == "capture_snapshot") {
        int device_id = std::max(
            1,
            std::min(jsonInt(body, "device_id", 1), kDeviceCount)
        );
        int level = jsonInt(body, "level", 2);
        if (level < 1) level = 1;
        if (level > 2) level = 2;
        refreshBus(device_id);
        const BusResult &bus = g_bus[device_id - 1];
        int rms_x100 = jsonInt(
            body,
            "rms_x100",
            bus.has_vib ? bus.rms_x100 : 70
        );
        int peak_x100 = jsonInt(
            body,
            "peak_x100",
            bus.has_vib ? bus.peak_x100 : 165
        );
        int site_code = jsonInt(body, "site_code", bus.site_code);
        const std::string path = captureFaultSnapshot(
            device_id,
            level,
            rms_x100 / 100.0,
            peak_x100 / 100.0,
            site_code,
            static_cast<int32_t>(rd(kOffRaw))
        );
        if (path.empty())
            return "{\"code\":1,\"msg\":\"snapshot write failed\",\"data\":{}}";
        return "{\"code\":0,\"msg\":\"ok\",\"data\":{\"snapshot\":\"" +
               path + "\"}}";
    }
    if (cmd == "waveform") {
        // 现场抓一段真实原始采样：MPU 采样周期 2ms(500Hz)，按同样节奏读 SENSOR_RAW
        int device_id = jsonInt(body, "device_id", 1);
        device_id = std::max(1, std::min(device_id, kDeviceCount));
        int n = jsonInt(body, "n", 128);
        if (n < 8) n = 8;
        if (n > 512) n = 512;

        refreshBus(device_id);
        const BusResult &bus = g_bus[device_id - 1];
        double amplitude_ms2 = 0.0;
        if (device_id > 1) {
            if (bus.has_vib)
                amplitude_ms2 = bus.rms_x100 / 100.0;
            else if (bus.ok)
                amplitude_ms2 = bus.site_max / 100.0;
            else if (directMultiNodeMode())
                amplitude_ms2 = 0.9;
        }

        std::string out = "{\"code\":0,\"msg\":\"ok\",\"data\":{\"device_id\":" +
                          std::to_string(device_id) +
                          ",\"n\":" + std::to_string(n) +
                          ",\"period_us\":2000,\"samples\":[";
        for (int i = 0; i < n; ++i) {
            if (i) out += ',';
            int32_t sample = static_cast<int32_t>(rd(kOffRaw));
            if (device_id > 1) {
                const double amp_lsb = amplitude_ms2 * kLsbPerG / kGToMs2;
                sample = static_cast<int32_t>(
                    amp_lsb * std::sin(2.0 * 3.141592653589793 * i / 24.0));
            } else {
                usleep(2000);
            }
            out += std::to_string(sample);
        }
        out += "]}}";
        return out;
    }
    if (cmd == "list_devices") {
        // Keep the list endpoint responsive: query_device refreshes a specific
        // slave, while this endpoint reports the latest cached state.
        std::string out = "{\"code\":0,\"msg\":\"ok\",\"data\":{\"devices\":[";
        for (int id = 1; id <= kDeviceCount; ++id) {
            const bool online = (id == 1) || g_bus[id - 1].ok;
            const char *source = (id == 1)
                ? "MPU6050 + PL"
                : "设备通道";
            if (id > 1) out += ',';
            out += "{\"device_id\":" + std::to_string(id) +
                   ",\"device_name\":\"" + std::to_string(id) + "号电机\"" +
                   ",\"online\":" + std::string(online ? "true" : "false") +
                   ",\"source\":\"" + source + "\"}";
        }
        out += "]}}";
        return out;
    }
    if (cmd == "query_device") {
        int id = jsonInt(body, "device_id", 1);
        if (id < 1 || id > kDeviceCount)
            return "{\"code\":1,\"msg\":\"device not exist\",\"data\":{\"device_id\":" +
                   std::to_string(id) + "}}";
        return snapJson(id);
    }
    return "{\"code\":-1,\"msg\":\"internal error: unknown cmd\",\"data\":{}}";
}

void serveConn(int fd) {
    std::string acc;
    char buf[512];
    while (true) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        acc.append(buf, static_cast<size_t>(n));
        size_t nl = acc.find('\n');
        if (nl == std::string::npos) {
            if (acc.size() > 4096) break;   // 防御
            continue;
        }
        std::string body = acc.substr(0, nl);
        std::string resp = handle(body) + "\n";
        ssize_t w = write(fd, resp.data(), resp.size());
        (void)w;
        break;   // 一问一答
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);
    std::signal(SIGPIPE, SIG_IGN);

    uint16_t port = 9000;
    if (argc >= 2) port = static_cast<uint16_t>(std::atoi(argv[1]));

    uint32_t base = 0x80000000u;
    if (const char *e = std::getenv("PL_REG_BASE")) base = static_cast<uint32_t>(std::strtoul(e, nullptr, 0));
    g_regs = mapPl(base);
    rs485Init();

    // 启动时先确认本地 MPU ID，坏 ID 就静默告警，避免一开机就误响
    const uint32_t startupStatus = rd(kOffStatus);
    g_localSensorValid =
        knownMpuId(static_cast<uint8_t>(rd(kOffWhoami) & 0xFF)) &&
        (startupStatus & 0x8) == 0;
    if (!g_localSensorValid) {
        wr(kOffAlertLow, 0xFFFFFFFFu);
        wr(kOffAlertHigh, 0xFFFFFFFFu);
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) { std::perror("socket"); return 1; }
    int yes = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind"); close(lfd); return 1;
    }
    if (listen(lfd, 8) < 0) { std::perror("listen"); close(lfd); return 1; }

    std::printf("[gateway] listening 0.0.0.0:%u  PL base=0x%08X  VERSION=0x%08X\n",
                port, base, rd(kOffVersion));
    std::fflush(stdout);

    while (!g_stop) {
        // 用 select 带超时等待连接，这样收到 SIGTERM 能及时退出
        // （直接在 accept() 上阻塞会因 glibc 的 BSD 语义自动重启，收不到信号）
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500 * 1000;
        int sel = select(lfd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (g_stop) break;
            continue;
        }
        if (sel == 0) continue;   // 超时，回头检查 g_stop

        sockaddr_in cli{};
        socklen_t cl = sizeof(cli);
        int cfd = accept(lfd, reinterpret_cast<sockaddr *>(&cli), &cl);
        if (cfd < 0) { if (g_stop) break; continue; }
        serveConn(cfd);
        close(cfd);
    }

    close(lfd);
    std::printf("[gateway] bye\n");
    return 0;
}
