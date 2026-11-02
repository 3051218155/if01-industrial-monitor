/*
 * motor_ctrl — PS 侧控制 DRV8833 双路电机（写 IF-01 AXI 寄存器）
 *
 * DRV8833 由 PL 输出 PWM/方向到 Y12/T9/U7/V6，PS 只写寄存器，不直接控 GPIO。
 *
 * 用法：
 *   ./motor_ctrl status
 *   ./motor_ctrl start  <1|2> [pwm=1000] [dir=0|1]
 *   ./motor_ctrl stop   <1|2|all>
 *   ./motor_ctrl demo                 # 两路 50% 正转 3 秒
 *   ./motor_ctrl hold  <1|2> [pwm] [dir]   # 持续刷新寄存器（查间歇停转）
 *   ./motor_ctrl run   <1|2> [pwm] [dir] [秒]  # 每 100ms 刷新，默认 10s
 *   ./motor_ctrl normal [pwm=1000] [ramp_ms=2000]  # A 路软启动并持续正常运行
 *   ./motor_ctrl watch [秒]           # 每秒打印寄存器读回（默认 30s）
 *
 * 环境变量：PL_REG_BASE（默认 0x80000000）
 */

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static constexpr size_t kMapSize = 0x1000;
static constexpr uint32_t kOffMotor1Ctrl = 0x40;
static constexpr uint32_t kOffMotor1Pwm = 0x44;
static constexpr uint32_t kOffMotor2Ctrl = 0x48;
static constexpr uint32_t kOffMotor2Pwm = 0x4C;
static constexpr uint32_t kPwmMax = 1000;

static volatile uint32_t* g_regs = nullptr;
static volatile std::sig_atomic_t g_stop_requested = 0;

static void on_stop_signal(int) {
    g_stop_requested = 1;
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

static uint32_t reg_read(uint32_t off) {
    return g_regs[off / 4];
}

static void reg_write(uint32_t off, uint32_t val) {
    g_regs[off / 4] = val;
    __sync_synchronize();
}

static uint32_t make_ctrl(bool enable, bool dir, bool brake) {
    uint32_t v = 0;
    if (enable) v |= 1u;
    if (dir) v |= 2u;
    if (brake) v |= 4u;
    return v;
}

static void motor_offsets(int id, uint32_t& ctrl_off, uint32_t& pwm_off) {
    if (id == 1) {
        ctrl_off = kOffMotor1Ctrl;
        pwm_off = kOffMotor1Pwm;
    } else if (id == 2) {
        ctrl_off = kOffMotor2Ctrl;
        pwm_off = kOffMotor2Pwm;
    } else {
        std::fprintf(stderr, "motor id must be 1 or 2\n");
        std::exit(1);
    }
}

static void cmd_status() {
    std::printf("PL_REG_BASE via mmap\n");
    std::printf("M1 ctrl=0x%08X pwm=%u\n", reg_read(kOffMotor1Ctrl), reg_read(kOffMotor1Pwm));
    std::printf("M2 ctrl=0x%08X pwm=%u\n", reg_read(kOffMotor2Ctrl), reg_read(kOffMotor2Pwm));
}

static void cmd_start(int id, uint32_t pwm, bool dir) {
    if (pwm > kPwmMax) {
        std::fprintf(stderr, "pwm max %u (0.1%% step)\n", kPwmMax);
        std::exit(1);
    }
    uint32_t ctrl_off, pwm_off;
    motor_offsets(id, ctrl_off, pwm_off);
    reg_write(pwm_off, pwm);
    reg_write(ctrl_off, make_ctrl(true, dir, false));
    std::printf("motor%d start pwm=%u dir=%d readback ctrl=0x%X pwm=%u\n",
                id, pwm, dir ? 1 : 0, reg_read(ctrl_off), reg_read(pwm_off));
}

static void cmd_stop(int id) {
    if (id == 0) {
        reg_write(kOffMotor1Ctrl, 0);
        reg_write(kOffMotor2Ctrl, 0);
        reg_write(kOffMotor1Pwm, 0);
        reg_write(kOffMotor2Pwm, 0);
        std::puts("motor all stop");
        return;
    }
    uint32_t ctrl_off, pwm_off;
    motor_offsets(id, ctrl_off, pwm_off);
    reg_write(ctrl_off, 0);
    reg_write(pwm_off, 0);
    std::printf("motor%d stop\n", id);
}

static void cmd_demo() {
    std::puts("demo: M1+M2 forward 50% for 3s");
    cmd_start(1, 500, false);
    cmd_start(2, 500, false);
    sleep(3);
    cmd_stop(0);
    std::puts("demo done");
}

static void refresh_motor(int id, uint32_t pwm, bool dir) {
    uint32_t ctrl_off, pwm_off;
    motor_offsets(id, ctrl_off, pwm_off);
    reg_write(pwm_off, pwm);
    reg_write(ctrl_off, make_ctrl(true, dir, false));
}

static void cmd_normal(uint32_t target_pwm, unsigned ramp_ms) {
    if (target_pwm > kPwmMax) {
        std::fprintf(stderr, "pwm max %u (0.1%% step)\n", kPwmMax);
        std::exit(1);
    }

    constexpr unsigned kStepMs = 20;
    unsigned ramp_steps = ramp_ms / kStepMs;
    if (ramp_steps == 0) ramp_steps = 1;
    uint32_t step = (target_pwm + ramp_steps - 1) / ramp_steps;
    if (step == 0) step = 1;

    g_stop_requested = 0;
    std::signal(SIGINT, on_stop_signal);
    std::signal(SIGTERM, on_stop_signal);
    std::signal(SIGHUP, on_stop_signal);

    if (target_pwm < 600) {
        std::puts("warning: PWM below 600 may not start the motor under load");
    }

    std::printf("A route normal run: AO1/AIN1(Y12)=PWM %u, "
                "AO2/AIN2(T9)=forward, ramp=%ums\n",
                target_pwm, ramp_ms);
    std::puts("Press Ctrl+C to stop. Registers 0x40/0x44 are refreshed every 100ms.");

    uint32_t pwm = 0;
    while (pwm < target_pwm && !g_stop_requested) {
        pwm += step;
        if (pwm > target_pwm) pwm = target_pwm;
        refresh_motor(1, pwm, false);
        usleep(kStepMs * 1000);
    }

    unsigned ticks = 0;
    while (!g_stop_requested) {
        refresh_motor(1, target_pwm, false);
        usleep(100000);
        if (++ticks % 10 == 0) {
            std::printf("normal: A route running, pwm=%u, ctrl=0x%X\n",
                        reg_read(kOffMotor1Pwm), reg_read(kOffMotor1Ctrl));
        }
    }

    std::puts("stop requested: ramping down A route");
    while (pwm > 0) {
        pwm = pwm > step ? pwm - step : 0;
        refresh_motor(1, pwm, false);
        usleep(kStepMs * 1000);
    }
    cmd_stop(1);
    std::puts("A route normal run stopped");
}

static void cmd_run(int id, uint32_t pwm, bool dir, unsigned seconds) {
    std::printf("run motor%d pwm=%u dir=%d for %us (refresh 100ms)\n", id, pwm,
                dir ? 1 : 0, seconds);
    const unsigned ticks = seconds * 10;
    for (unsigned i = 0; i < ticks; ++i) {
        refresh_motor(id, pwm, dir);
        usleep(100000);
    }
    cmd_stop(id);
    std::printf("run done readback ctrl=0x%X pwm=%u\n",
                reg_read(id == 1 ? kOffMotor1Ctrl : kOffMotor2Ctrl),
                reg_read(id == 1 ? kOffMotor1Pwm : kOffMotor2Pwm));
}

static void cmd_hold(int id, uint32_t pwm, bool dir) {
    std::printf("hold motor%d pwm=%u dir=%d (Ctrl+C stop). 若仍间歇停转→优先查接线/VM\n", id,
                pwm, dir ? 1 : 0);
    for (;;) {
        refresh_motor(id, pwm, dir);
        sleep(1);
    }
}

static void cmd_watch(unsigned seconds) {
    std::printf("watch %us (每秒读 M1/M2 寄存器，若 pwm/ctrl 自己变 0→软件/PL 在改)\n", seconds);
    for (unsigned i = 0; i < seconds; ++i) {
        std::printf("[%02u] M1 ctrl=0x%08X pwm=%u | M2 ctrl=0x%08X pwm=%u\n",
                    i + 1, reg_read(kOffMotor1Ctrl), reg_read(kOffMotor1Pwm),
                    reg_read(kOffMotor2Ctrl), reg_read(kOffMotor2Pwm));
        sleep(1);
    }
}

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s status\n"
        "  %s start <1|2> [pwm] [dir]\n"
        "  %s stop <1|2|all>\n"
        "  %s demo\n"
        "  %s hold <1|2> [pwm] [dir]\n"
        "  %s run <1|2> [pwm] [dir] [seconds]\n"
        "  %s normal [pwm] [ramp_ms]\n"
        "  %s watch [seconds]\n",
        prog, prog, prog, prog, prog, prog, prog, prog);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const uint32_t base = []() {
        const char* env = std::getenv("PL_REG_BASE");
        return env ? parse_u32(env, "PL_REG_BASE") : 0x80000000U;
    }();
    g_regs = map_pl_base(base);

    const std::string cmd = argv[1];
    if (cmd == "status") {
        cmd_status();
        return 0;
    }
    if (cmd == "start") {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        const int id = static_cast<int>(parse_u32(argv[2], "motor id"));
        uint32_t pwm = 1000;
        bool dir = false;
        if (argc >= 4) pwm = parse_u32(argv[3], "pwm");
        if (argc >= 5) dir = parse_u32(argv[4], "dir") != 0;
        cmd_start(id, pwm, dir);
        return 0;
    }
    if (cmd == "stop") {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        if (std::strcmp(argv[2], "all") == 0) {
            cmd_stop(0);
        } else {
            cmd_stop(static_cast<int>(parse_u32(argv[2], "motor id")));
        }
        return 0;
    }
    if (cmd == "demo") {
        cmd_demo();
        return 0;
    }
    if (cmd == "normal") {
        uint32_t pwm = 1000;
        unsigned ramp_ms = 2000;
        if (argc >= 3) pwm = parse_u32(argv[2], "pwm");
        if (argc >= 4) ramp_ms = parse_u32(argv[3], "ramp_ms");
        cmd_normal(pwm, ramp_ms);
        return 0;
    }
    if (cmd == "hold") {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        const int id = static_cast<int>(parse_u32(argv[2], "motor id"));
        uint32_t pwm = 500;
        bool dir = false;
        if (argc >= 4) pwm = parse_u32(argv[3], "pwm");
        if (argc >= 5) dir = parse_u32(argv[4], "dir") != 0;
        cmd_hold(id, pwm, dir);
        return 0;
    }
    if (cmd == "run") {
        if (argc < 3) {
            usage(argv[0]);
            return 1;
        }
        const int id = static_cast<int>(parse_u32(argv[2], "motor id"));
        uint32_t pwm = 1000;
        bool dir = false;
        unsigned sec = 10;
        if (argc >= 4) pwm = parse_u32(argv[3], "pwm");
        if (argc >= 5) dir = parse_u32(argv[4], "dir") != 0;
        if (argc >= 6) sec = parse_u32(argv[5], "seconds");
        cmd_run(id, pwm, dir, sec);
        return 0;
    }
    if (cmd == "watch") {
        unsigned sec = 30;
        if (argc >= 3) sec = parse_u32(argv[2], "seconds");
        cmd_watch(sec);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
