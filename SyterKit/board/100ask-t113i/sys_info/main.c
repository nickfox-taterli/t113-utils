#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <types.h>

#include <log.h>
#include <common.h>
#include <jmp.h>

#include <sys-dram.h>
#include <sys-sid.h>
#include <sys-sdcard.h>

#include "ff.h"   // FatFs

// ---------------- 配置 ----------------

// U-Boot 的 TEXT_BASE，必须和 U-Boot 配置里一致
#define UBOOT_TEXT_BASE   0x42e00000U

// TF 卡 vfat 分区里的文件名（根目录）
#define UBOOT_FILENAME    "u-boot.bin"

// 一次读多少字节，不需要太讲究
#define UBOOT_READ_CHUNK  0x20000U     // 128KB

// ---------------- 外部符号 ----------------

extern sunxi_serial_t uart_dbg;
extern dram_para_t    dram_para;

extern sdhci_t        sdhci0;

// ---------------- 从 FAT 加载 u-boot.bin ----------------

static int load_uboot_file(const char *filename, uint8_t *dest, uint32_t *out_size)
{
    FATFS   fs;
    FIL     file;
    FRESULT fr;
    UINT    br;
    uint8_t *p = dest;

    // 挂载第一个 FAT 分区
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printk_error("FATFS: mount fail: %d\n", fr);
        return -1;
    }

    printk_info("FATFS: opening %s\n", filename);
    fr = f_open(&file, filename, FA_OPEN_EXISTING | FA_READ);
    if (fr != FR_OK) {
        printk_error("FATFS: open fail: %d\n", fr);
        f_mount(0, "", 0);
        return -1;
    }

    // 循环读取文件到内存
    do {
        br = 0;
        fr = f_read(&file, p, UBOOT_READ_CHUNK, &br);
        if (fr != FR_OK) {
            printk_error("FATFS: read fail: %d\n", fr);
            f_close(&file);
            f_mount(0, "", 0);
            return -1;
        }
        p += br;
    } while (br == UBOOT_READ_CHUNK);  // 读到少于 chunk 说明到文件尾

    f_close(&file);
    f_mount(0, "", 0);

    if (out_size) {
        *out_size = (uint32_t)(p - dest);
    }

    return 0;
}

static void jump_to_uboot(uint32_t entry)
{
    __asm__ volatile (
        "mov    r0, #0\n"      // 和 SPL 一样，不给参数
        "mov    r1, #0\n"
        "mov    r2, #0\n"
        "mov    r3, #0\n"
        "mov    lr, #0\n"      // 防止 U-Boot 返回还乱跳
        "bx     %0\n"
        :
        : "r"(entry)
        : "r0", "r1", "r2", "r3", "lr", "memory"
    );
}

// ---------------- 主入口 ----------------

int main(void)
{
    uint32_t uboot_size = 0;
    uint8_t *uboot_addr = (uint8_t *)UBOOT_TEXT_BASE;

    // 1. 基础初始化：串口 + clk + DRAM
    sunxi_serial_init(&uart_dbg);
    show_banner();

    sunxi_clk_init();
    sunxi_dram_init(&dram_para);
    sunxi_clk_dump();

    printk_info("Loader: prepare to load U-Boot from SD\n");

    // 2. 初始化 SD 控制器 + 卡
    if (sunxi_sdhci_init(&sdhci0) != 0) {
        printk_error("SMHC: %s init failed\n", sdhci0.name);
        goto hang;
    }

    if (sdmmc_init(&card0, &sdhci0) != 0) {
        printk_error("SMHC: card init failed\n");
        goto hang;
    }

    printk_info("SMHC: SD card ready, loading %s...\n", UBOOT_FILENAME);

    // 3. 从 TF 卡 vfat 分区读取 u-boot.bin 到 UBOOT_TEXT_BASE
    if (load_uboot_file(UBOOT_FILENAME, uboot_addr, &uboot_size) != 0) {
        printk_error("Load %s failed\n", UBOOT_FILENAME);
        goto hang;
    }

    // 4. 打印一部分内容确认
    // dump_uboot_header(uboot_addr, uboot_size);

    // 5. 可选：关中断（和 cache/MMU，如果你有开的话）
    __asm__ volatile("cpsid if" : : : "memory");

    // 6. 跳转到 U-Boot
    printk_info("Jumping to U-Boot at 0x%08x ...\n", (unsigned int)UBOOT_TEXT_BASE);

    jump_to_uboot(UBOOT_TEXT_BASE);

hang:
    printk_error("Loader hang.\n");
    while (1) {
    }

    return 0;
}
