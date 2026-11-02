/*
 * pl_regs — PS 侧 AXI-Lite 寄存器读写小工具（IF-01 v0.1）
 *
 * 用法：
 *   ./pl_regs read  <offset_hex>              读 32bit 寄存器（相对基地址）
 *   ./pl_regs write <offset_hex> <value_hex>    写 32bit
 *   ./pl_regs dump  [count]                     从 0x00 起 dump N 个 word（默认 16）
 *
 * 环境变量：
 *   PL_REG_BASE  物理基地址，默认 0x80000000（DR1M90 M_AXI_GP0）
 *
 * 示例（PL 在 0x08 回魔数 0xDEADBEEF）：
 *   PL_REG_BASE=0x80000000 ./pl_regs read 0x08
 *
 * 注意：需 root；通过 /dev/mem mmap。后续可换 UIO 或字符设备驱动。
 */

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

static constexpr size_t kMapSize = 0x1000; /* 4 KiB，覆盖 IF-01 当前寄存器表 */

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

static void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage:\n"
        "  %s read  <offset>\n"
        "  %s write <offset> <value>\n"
        "  %s dump  [word_count]\n"
        "\n"
        "  offset/value: hex (0x08) or decimal\n"
        "  PL_REG_BASE env, default 0x80000000\n",
        prog, prog, prog);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const uint32_t base_phys = []() {
        const char* env = std::getenv("PL_REG_BASE");
        return env ? parse_u32(env, "PL_REG_BASE") : 0x80000000U;
    }();

    volatile uint32_t* regs = map_pl_base(base_phys);
    const std::string cmd = argv[1];

    if (cmd == "read") {
        if (argc != 3) {
            usage(argv[0]);
            return 1;
        }
        const uint32_t off = parse_u32(argv[2], "offset");
        if (off >= kMapSize || (off % 4) != 0) {
            std::fprintf(stderr, "offset out of range or not 4-byte aligned: 0x%08X\n", off);
            return 1;
        }
        const uint32_t val = regs[off / 4];
        std::printf("base=0x%08X off=0x%08X val=0x%08X (%u)\n",
                    base_phys, off, val, val);
        return 0;
    }

    if (cmd == "write") {
        if (argc != 4) {
            usage(argv[0]);
            return 1;
        }
        const uint32_t off = parse_u32(argv[2], "offset");
        const uint32_t val = parse_u32(argv[3], "value");
        if (off >= kMapSize || (off % 4) != 0) {
            std::fprintf(stderr, "offset out of range or not 4-byte aligned: 0x%08X\n", off);
            return 1;
        }
        regs[off / 4] = val;
        __sync_synchronize();
        const uint32_t rb = regs[off / 4];
        std::printf("write base=0x%08X off=0x%08X val=0x%08X readback=0x%08X\n",
                    base_phys, off, val, rb);
        return 0;
    }

    if (cmd == "dump") {
        unsigned count = 16;
        if (argc >= 3) {
            count = parse_u32(argv[2], "count");
        }
        if (count == 0 || count * 4 > kMapSize) {
            std::fprintf(stderr, "count too large (max %zu words)\n", kMapSize / 4);
            return 1;
        }
        std::printf("dump base=0x%08X (%u words)\n", base_phys, count);
        for (unsigned i = 0; i < count; ++i) {
            std::printf("  [0x%02X] 0x%08X\n", i * 4, regs[i]);
        }
        return 0;
    }

    usage(argv[0]);
    return 1;
}
