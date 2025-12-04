# T113i MiniEVM 主线 Linux / C906 AMP 实验说明

本仓库是我在 **Allwinner T113i MiniEVM** 上折腾主线 Linux(6.18)和 C906 小核 AMP 的整理记录.

当前状态:

- 主核:Cortex-A7 双核,主线内核 6.18 自编译
- 显示:**DSI → LT8912B → HDMI 强制分辨率输出** 已经点亮
- USB:基本功能测试通过(OTG 设备模式,USB1 Host)
- 网络:RGMII 千兆网口已打通
- C906:remoteproc + rpmsg 通了,可以和 A7 互相收发消息
- 板级 DTS:`arch/arm/boot/dts/allwinner/sun8i-t113i-minievm.dts`

下面是当前"什么是主线原生就支持的,什么是我自己移植的,什么已经在板子上跑过"的整理.


## 功能支持矩阵

> 说明:  
> - **主线支持**:上游已经有对应 IP/功能的驱动,本板只是正常接线+DTS 描述  
> - **移植支持**:需要自己加 DTS,驱动或额外 glue code,才在本板上可用  
> - **板上实测**:实际在 MiniEVM 上跑过而且确认工作正常(至少通过简单功能测试)  

| 功能模块 | 主线支持 | 移植支持 | 板上实测 | 备注 |
| :-- | :--: | :--: | :--: | :-- |
| SoC 基础支持(T113s,SMP,GIC,CCU,PIO 等) | ✅ |  | ✅ | 复用 `sun8i-t113s.dtsi`,按主线方式启核 |
| MiniEVM 板级 DTS(供电,电源树,引脚复用等) |  | ✅ | ✅ | 本板专用:`sun8i-t113i-minievm.dts` |
| UART0 调试串口(PG17/PG18) | ✅ |  | ✅ | `serial0 = &uart0`,115200n8 串口 log |
| TF 卡(MMC0) | ✅ |  | ✅ | 用于启动 & 根文件系统,`mmc0` DTS 已打开 |
| GPIO LED(PG11) |  | ✅ | ✅ | `green:status` 上电默认点亮 |
| DCXO 24MHz / 基本时钟 | ✅ |  | ✅ | `&dcxo { clock-frequency = <24000000>; };` |
| 看门狗 WDT | ✅ |  | ⏱ | DTS 已使能,后续补充长时间跑测 |
| USB0 OTG 设备模式 | ✅ |  | ✅ | `&usb_otg { dr_mode = "peripheral"; };`,可枚举为 gadget |
| USB1 Host(EHCI1/OHCI1,外供 VBUS) | ✅ |  | ✅ | `reg_usb1_vbus` + `usbphy`,U 盘/HID 可用 |
| RGMII 千兆以太网(外部 PHY) | ✅ |  | ✅ | `phy-mode = "rgmii"`,DHCP/ssh/apt 等正常 |
| I2C2(PE12/PE13,用于 LT8912B) | ✅ |  | ✅ | 与 LT8912B 通信正常 |
| DSI 控制器 | ✅ |  | ✅ | 4-lane 输出到 LT8912B |
| LT8912B DSI→HDMI(强制 720p30) |  | ✅ | ✅ | 通过 `lontium,force-mode = "720p30"` 固定分辨率,无 EDID/DDC |
| HDMI 连接器(Type-A) |  | ✅ | ✅ | 目前只做视频输出,音频后续再说 |
| C906 reserved-memory(0x41000000@1M) |  | ✅ | ✅ | `c906_reserved` reserved-memory 用于共享内存/RPMsg |
| MSGBOX(ARM ↔ C906) | ✅ | ✅ | ✅ | 主线有 IP 驱动,本工程有裁剪版 + DTS 三个基址/中断配置 |
| C906 remoteproc 启动/停止 |  | ✅ | ✅ | 节点 `c906: rproc@6010000`,可通过 sysfs 控制 |
| C906 ↔ A7 RPMsg 通信 |  | ✅ | ✅ | 用户态配合 `rpmsg_open` / `rpmsg_ping` 测试通过 |
| HiFi4 remoteproc 启动/停止 |  | ✅ | ✅ | 节点 `dsp: rproc@1700000`,可通过 sysfs 控制 |
| HiFi4 ↔ A7 RPMsg 通信 |  |  | 📅 | 等待实现 |
| USB-C CC / role 切换 |  |  |  | 板上 USB-C 只硬连成 UFP,暂不考虑 DRD |
| 音频(I2S / Codec) |  |  |  | 板子上未接 |
| 其他外设(SPI,CAN,ADC,PWM...) |  |  |  | 预留占位,后续按需补充 |


## 工程整体说明

整个 T113i MiniEVM 主线方案由多个仓库组成,本仓库负责 **C906 侧固件 + 一点辅助用户态工具**,配合主线 Linux / U‑Boot 一起实现 AMP:

- C906 裸机固件:`src/` + `lib/` + `link.ld`  
  - 运行在 C906 上,实现 resource_table + virtio + RPMsg echo 服务  
  - 链接到 `0x41000000@1M` 的 reserved‑memory 区域,供 Linux remoteproc 直接加载 `c906.elf`
- Linux 用户态测试工具:`cpux_code/`  
  - `rpmsg_open`:通过 `/dev/rpmsg_ctrlX` 创建 endpoint  
  - `rpmsg_ping`:向 `/dev/rpmsgX` 发送字符串并等待 C906 回 echo
- SyterKit 子模块:`SyterKit/`  
  - 上游 SyterKit 工程,包含 T113 / 100ask‑t113i 的板级初始化代码  
  - 本仓库主要复用其 DRAM/UART 等早期初始化和工具链配置

相关配套仓库:

- Linux 内核:<https://github.com/nickfox-taterli/t113-linux>
- U‑Boot:<https://github.com/nickfox-taterli/t113-uboot>


## 编译说明(C906 固件)

1. 准备 RISC‑V 工具链(例如玄铁 Xuantie‑900 gcc),记下安装路径.  
   默认的 `CMakeLists.txt` 中把 `RISCV_ROOT_PATH` 写成了本机路径,需要按自己环境覆盖.

2. 在仓库根目录执行:

```bash
mkdir -p build
cd build
cmake -DRISCV_ROOT_PATH=/opt/Xuantie-900-gcc-linux-XXX ..
make -j"$(nproc)"
```

3. 编译完成后,会得到:

- `build/src/c906.elf`:提供给 Linux `remoteproc` 加载的 ELF 固件

如需 bin 形式,可以额外执行(可选):

```bash
"${RISCV_ROOT_PATH}/bin/riscv64-unknown-linux-gnu-objcopy" \
  -O binary build/src/c906.elf build/src/c906.bin
```

## 编译说明(HiFi4 固件)

**注意,这个不是正经的Cadence固件,是开源测试固件!**

1. 工具链下载地址 https://github.com/YuzukiHD/FreeRTOS-HIFI4-DSP/releases/download/Toolchains/xtensa-hifi4-dsp.tar.gz

2. 进入FreeRTOS-HiFi4-DSP目录进行编译

3. 加载地址是0x4FC00000,而不是0x40900000,因为后者是内核区.

## 编译 A7 侧 RPMsg 小工具

在主核 Linux 上,用 arm-linux-gnueabihf-gcc 编译即可:

```bash
arm-linux-gnueabihf-gcc -O2 -Wall cpux_code/rpmsg_open.c -o rpmsg_open
arm-linux-gnueabihf-gcc -O2 -Wall cpux_code/rpmsg_ping.c -o rpmsg_ping
```

简单使用示例(设备节点名称按自己系统调整):

```bash
# 1. 启动 C906 remoteproc(以 remoteproc0 为例)
echo start     > /sys/class/remoteproc/remoteproc0/state

# 2. 创建 endpoint
#    - 名字要和 C906 固件中的 RPMsg NS 名一致:c906-echo
#    - dst 地址要和 C906 固件中的 LOCAL_EPT_ADDR 一致:0x1
./rpmsg_open /dev/rpmsg_ctrl0 c906-echo 0x1

# 3. 做一次 echo 测试
./rpmsg_ping /dev/rpmsg0 "hello from A7"
```

## 目录结构

```text
.
├── c906                # 在 C906 运行的 RPMsg 测试程序
├── cpux_code           # 在 A7 Linux 用户态运行的 RPMsg 测试程序
└── SyterKit            # SyterKit 子模块及其 T113 / 100ask‑t113i 板级代码
```
