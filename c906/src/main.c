#include "rpmsg.h"
#include "rsc_table.h"
#include <byteorder.h>
#include <config.h>
#include <endian.h>
#include <io.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys-clock.h>
#include <sys-uart.h>
#include <types.h>
#include <uart.h>

/*
 * 调试打印开关:
 *  - 定义 C906_RPMSG_DEBUG 时,DBG_PRINTF 映射到 sys_uart_printf
 *  - 未定义时,DBG_PRINTF 编译为空,不产生任何 UART 输出
 */
#ifdef C906_RPMSG_DEBUG
#define DBG_PRINTF sys_uart_printf
#else
#define DBG_PRINTF(...) do { } while (0)
#endif

/* C906 侧 MSGBOX 寄存器基址 */
#define MSGBOX_BASE_RV      0x0601f000U
/* ARM(CPUX) 侧 MSGBOX 寄存器基址(从 C906 直接访问) */
#define MSGBOX_BASE_CPUX    0x03003000U
/* MSGBOX 寄存器窗口大小,用于地址合法性检查 */
#define MSGBOX_REGION_SIZE  0x1000U

/* Sunxi MSGBOX 寄存器偏移和位定义 */
#define SUNXI_MSGBOX_OFFSET(n)             (0x100 * (n))
#define SUNXI_MSGBOX_READ_IRQ_ENABLE(n)    (0x20 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_READ_IRQ_STATUS(n)    (0x24 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_ENABLE(n)   (0x30 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_STATUS(n)   (0x34 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_MSG_STATUS(n, p)      (0x60 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))
#define SUNXI_MSGBOX_MSG_FIFO(n, p)        (0x70 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))

#define RD_IRQ_EN_MASK     0x1
#define RD_IRQ_EN_SHIFT(p) ((p) * 2)

#define RD_IRQ_PEND_MASK   0x1
#define RD_IRQ_PEND_SHIFT(p) ((p) * 2)

#define MSG_NUM_MASK       0xF
#define MSG_NUM_SHIFT      0

#define LOCAL_ID  2 /* C906 CPU ID(硬件定义) */

/*
 * MSGBOX 路由(ARM0 <-> C906(2)):
 *  - ARM -> C906: ARM 写 coef_n = 0, C906 看到的是 local_n = 0
 *  - C906 -> ARM: C906 写 coef_n = 1, ARM 在 local_n = 1 上接收
 */
#define LOCAL_N   0 /* C906 本地接收通道 (ARM -> C906) */
#define REMOTE_ID 0 /* 远端 CPU: ARM */
/* ARM 在 local_n=1 上接收来自 C906 的通知 */
#define REMOTE_N  1 /* C906 发送到 ARM 使用的通道 (C906 -> ARM) */
#define CHAN_P    0 /* 使用的 channel P 索引 */

#define VRING_ALIGN 4096U
#define VRING_NUM   16U

#define VRING0_DA (resources.vring[0].da) /* TX vring 物理地址 (remote->host) */
#define VRING1_DA (resources.vring[1].da) /* RX vring 物理地址 (host->remote) */

/* 共享内存区域,用于 RPMsg buffer */
#define SHM_BASE_ADDR   0x41000000U
#define SHM_SIZE_BYTES  0x00100000U
#define SHM_LIMIT_ADDR  (SHM_BASE_ADDR + SHM_SIZE_BYTES)

/* virtio vdev 状态位: 驱动就绪 */
#define VIRTIO_CONFIG_S_DRIVER_OK 0x04

/* vring 描述符 flags */
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* 本地 endpoint 地址和服务名(echo) */
#define LOCAL_EPT_ADDR  0x1
#define RPMSG_ECHO_NAME "c906-echo"

/*
 * 标准 virtio vring 结构定义(与 Linux/virtio 一致)
 */
struct vring_desc {
	uint64_t addr;  /* buffer 物理地址 */
	uint32_t len;   /* buffer 长度 */
	uint16_t flags; /* VRING_DESC_F_* 标志 */
	uint16_t next;  /* 下一个描述符索引(若 F_NEXT 置位) */
} __attribute__((packed));

struct vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[VRING_NUM];
} __attribute__((packed));

struct vring_used_elem {
	uint32_t id;  /* 对应 avail ring 中的 desc 索引 */
	uint32_t len; /* 实际使用的长度 */
} __attribute__((packed));

struct vring_used {
	uint16_t              flags;
	uint16_t              idx;
	struct vring_used_elem ring[VRING_NUM];
} __attribute__((packed));

/* vring 封装: 指向 desc/avail/used 的指针 + 数量 */
struct vring {
	struct vring_desc  *desc;
	struct vring_avail *avail;
	struct vring_used  *used;
	uint16_t            num;
};

/* 控制结构:包含 vring 和 last_avail 位置 */
struct vr_ctrl {
	struct vring vr;
	uint16_t     last_avail;
};

/* TX vring: remote -> host (rvq) */
static struct vr_ctrl vr_tx;
/* RX vring: host -> remote (svq) */
static struct vr_ctrl vr_rx;

/*
 * MSGBOX 地址合法性检查:
 *  只允许访问 C906 本地和 CPUX 这两块,其余视为错误访问.
 */
static int msgbox_addr_valid(uint32_t addr)
{
	if ((addr >= MSGBOX_BASE_RV && addr < MSGBOX_BASE_RV + MSGBOX_REGION_SIZE) ||
	    (addr >= MSGBOX_BASE_CPUX && addr < MSGBOX_BASE_CPUX + MSGBOX_REGION_SIZE)) {
		return 1;
	}

	/* 非预期的地址访问,打印出来方便查错 */
	DBG_PRINTF("MSGBOX: invalid addr 0x%08x\r\n", addr);
	return 0;
}

/* 简单封装的 MSGBOX 读写接口 */
static inline void mb_write(uint32_t base, uint32_t off, uint32_t val)
{
	uint32_t addr = base + off;

	if (!msgbox_addr_valid(addr)) {
		DBG_PRINTF("MSGBOX: invalid write addr=0x%08x val=0x%08x\r\n",
			   addr, val);
		return;
	}
	write32(addr, val);
}

static inline uint32_t mb_read(uint32_t base, uint32_t off)
{
	uint32_t addr = base + off;

	if (!msgbox_addr_valid(addr)) {
		DBG_PRINTF("MSGBOX: invalid read addr=0x%08x\r\n", addr);
		return 0;
	}
	return read32(addr);
}

/*
 * 配置 C906 本地 MSGBOX:
 *  - 打开 LOCAL_N/P 的读 IRQ 使能
 *  - 清 pending 状态
 */
static void msgbox_init(void)
{
	uint32_t val;

	/* 启用 C906 本地 MSGBOX 的 RX IRQ: N=0, P=0 */
	val = mb_read(MSGBOX_BASE_RV, SUNXI_MSGBOX_READ_IRQ_ENABLE(LOCAL_N));
	val |= (RD_IRQ_EN_MASK << RD_IRQ_EN_SHIFT(CHAN_P));
	mb_write(MSGBOX_BASE_RV, SUNXI_MSGBOX_READ_IRQ_ENABLE(LOCAL_N), val);

	/* 清当前的 pending 位,避免一上来就误触发 */
	mb_write(MSGBOX_BASE_RV, SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N),
		 RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P));
}

/*
 * 轮询 MSGBOX,看 ARM 是否给了 "kick"(vqid).
 * 有消息时:
 *   - 从 MSG FIFO 读出 vqid
 *   - 写 1 清 pending 位
 * 返回:
 *   - 1: 有消息
 *   - 0: 无消息
 */
static int msgbox_poll(uint32_t *vqid)
{
	uint32_t pend;

	pend = mb_read(MSGBOX_BASE_RV, SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N));
	if (!(pend & (RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P)))) {
		/*
		 * 硬件行为比较迷:
		 *  - 实测如果这里不读一次 FIFO,后续就"跑不起来"
		 *  - 手册描述为 PEEK,但实际这个读会丢弃一条数据
		 *  - 我们在"无 pending"时读一次,然后直接返回 0
		 *
		 * 请勿轻易删除,这是针对当前硬件的绕过方案.
		 */
		mb_read(MSGBOX_BASE_RV, SUNXI_MSGBOX_MSG_FIFO(LOCAL_N, CHAN_P));
		return 0;
	}

	*vqid = mb_read(MSGBOX_BASE_RV, SUNXI_MSGBOX_MSG_FIFO(LOCAL_N, CHAN_P));
	mb_write(MSGBOX_BASE_RV, SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N),
		 RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P));

	return 1;
}

/*
 * 通知 ARM(host),某个 vring 有新 used buffer:
 *  - 读取远端通道的 MSG_STATUS 看 FIFO 是否有空位(最多 8)
 *  - 有空则写入 vqid
 *
 * 注意:这里访问 CPUX 视角的 MSGBOX(0x03003000), REMOTE_N=1.
 */
static void msgbox_kick_host(uint32_t vqid)
{
	uint32_t used;

	used = mb_read(MSGBOX_BASE_CPUX,
		       SUNXI_MSGBOX_MSG_STATUS(REMOTE_N, CHAN_P));
	used = (used >> MSG_NUM_SHIFT) & MSG_NUM_MASK;
	if (used >= 8)
		return; /* FIFO 满了,直接丢弃这次 kick */

	/* 标准 virtio kick: 写 vqid */
	mb_write(MSGBOX_BASE_CPUX, SUNXI_MSGBOX_MSG_FIFO(REMOTE_N, CHAN_P), vqid);
}

/*
 * 根据给定物理基址解析出 vring 的 desc/avail/used 布局:
 *
 *   base
 *    ├─ desc[0..num-1]
 *    ├─ avail (含 ring[num])
 *    └─ 对齐到 align 后的 used
 */
static void vring_setup(struct vr_ctrl *vc, uintptr_t base,
			uint16_t num, uint32_t align)
{
	uintptr_t avail;
	uintptr_t used;

	vc->vr.desc = (struct vring_desc *)base;

	avail = base + sizeof(struct vring_desc) * num;
	vc->vr.avail = (struct vring_avail *)avail;

	/* used 紧跟在 avail->ring[num] 后面,再做一次对齐 */
	used = (uintptr_t)&vc->vr.avail->ring[num];
	used = (used + align - 1) & ~(uintptr_t)(align - 1);
	vc->vr.used = (struct vring_used *)used;

	vc->vr.num = num;
	vc->last_avail = 0;
}

/*
 * 从 avail ring 中取出下一个可用 buffer 的 desc 索引:
 *  - avail->idx 为 host 写入的 "下一个可用位置" 索引
 *  - last_avail 为本地消费进度
 * 若没有新 buffer,返回 -1;否则通过 desc_idx 返回索引.
 */
static int vring_get_avail(struct vr_ctrl *vc, uint16_t *desc_idx)
{
	uint16_t idx = vc->vr.avail->idx;

	if (vc->last_avail == idx)
		return -1;

	*desc_idx = vc->vr.avail->ring[vc->last_avail % vc->vr.num];
	vc->last_avail++;
	return 0;
}

/*
 * 向 used ring 中添加一个已完成的 buffer:
 *  - 写 ring[used_idx % num] 的 id 和 len
 *  - 通过内存屏障保证写入对 host 可见
 *  - 增加 used->idx
 */
static void vring_add_used(struct vring *vr, uint16_t id, uint32_t len)
{
	uint16_t used_idx = vr->used->idx;

	vr->used->ring[used_idx % vr->num].id  = id;
	vr->used->ring[used_idx % vr->num].len = len;

	/* 确保 ring 内容先于 idx 对 host 可见 */
	__sync_synchronize();

	vr->used->idx = used_idx + 1;
}

/*
 * 通过 TX vring 向指定 dst endpoint 发送一帧 rpmsg:
 *  - 从 vr_tx.avail 中取一个 desc
 *  - 在该 buffer 头部填 rpmsg_hdr,其后是 payload
 *  - 写入 used ring
 *  - 最后通过 msgbox_kick_host 通知 ARM
 */
static int rpmsg_sendto(uint32_t vqid, uint32_t dst, const void *data, uint16_t len)
{
	uint16_t            desc_idx;
	struct vring_desc  *desc;
	struct rpmsg_hdr   *hdr;

	if (vring_get_avail(&vr_tx, &desc_idx))
		return -1;

	desc = &vr_tx.vr.desc[desc_idx];
	hdr  = (struct rpmsg_hdr *)(uintptr_t)desc->addr;

	/* 防止 payload 长度超过 desc buffer 长度 */
	if (len + sizeof(*hdr) > desc->len)
		len = desc->len - sizeof(*hdr);

	hdr->src      = LOCAL_EPT_ADDR;
	hdr->dst      = dst;
	hdr->reserved = 0;
	hdr->len      = len;
	hdr->flags    = 0;

	memcpy(hdr->data, data, len);

	vring_add_used(&vr_tx.vr, desc_idx, len + sizeof(*hdr));

	/* 告诉 host: TX vring 有新数据 (这里 vqid=0,与 host 协议一致) */
	msgbox_kick_host(vqid);

	return 0;
}

/*
 * 向 host 发布 NS (name service) 消息:
 *  - 告诉 host: 本地有一个名为 "c906-echo" 的 endpoint,地址为 LOCAL_EPT_ADDR
 */
static int rpmsg_send_ns(void)
{
	struct rpmsg_ns_msg ns;
	int                 ret;

	memset(&ns, 0, sizeof(ns));
	strncpy(ns.name, RPMSG_ECHO_NAME, RPMSG_NAME_SIZE - 1);
	ns.addr  = LOCAL_EPT_ADDR;
	ns.flags = RPMSG_NS_CREATE;

	ret = rpmsg_sendto(0, RPMSG_NS_ADDR, &ns, sizeof(ns));
	if (ret)
		DBG_PRINTF("rpmsg_send_ns failed (%d)\r\n", ret);

	return ret ? -1 : 0;
}

/*
 * 处理来自 host 的 RX 消息:
 *  - 遍历 vr_rx.avail 获取 desc
 *  - 做共享内存地址合法性检查(防止 host 提供错误地址)
 *  - 打印 rpmsg 头和 payload
 *  - 将 payload 原样 echo 回 src endpoint
 *  - 将该 desc 加入 used ring,并再次 kick host
 */
static void process_host_messages(uint32_t vqid)
{
	uint16_t           desc_idx;
	struct vring_desc *desc;
	static int         rx_invalid_warned;
	struct rpmsg_hdr  *hdr;
	uint32_t           addr_lo;
	uint32_t           addr_hi;
	uint64_t           addr;
	uint16_t           payload_len;
	uint16_t           max_payload;
	const uint8_t     *payload;

	while (vring_get_avail(&vr_rx, &desc_idx) == 0) {
		desc = &vr_rx.vr.desc[desc_idx];
		hdr  = (struct rpmsg_hdr *)(uintptr_t)desc->addr;
		payload = hdr->data;

		addr    = desc->addr;
		addr_lo = (uint32_t)(addr & 0xffffffffu);
		addr_hi = (uint32_t)((addr >> 32) & 0xffffffffu);

		/* 检查 buf 地址是否在预期的共享内存区域内 */
		if (addr < SHM_BASE_ADDR || addr >= SHM_LIMIT_ADDR) {
			if (!rx_invalid_warned) {
				DBG_PRINTF("RX invalid buf: idx=%d addr=0x%08x%08x len=%d\r\n",
					   (int)desc_idx, addr_hi, addr_lo, (int)desc->len);
				DBG_PRINTF("RX state: avail_idx=%d last_avail=%d\r\n",
					   (int)vr_rx.vr.avail->idx, (int)vr_rx.last_avail);
				rx_invalid_warned = 1;
			}
			break;
		}

		DBG_PRINTF("RX desc=%d len=%d addr=0x%08x%08x\r\n",
			   (int)desc_idx, (int)desc->len, addr_hi, addr_lo);

		/* 计算合法的 payload 长度 */
		payload_len = hdr->len;
		max_payload = 0;
		if (desc->len > sizeof(*hdr))
			max_payload = desc->len - sizeof(*hdr);
		if (payload_len > max_payload)
			payload_len = max_payload;

		DBG_PRINTF("RX hdr: src=0x%x dst=0x%x len=%d flags=0x%x\r\n",
			   (unsigned int)hdr->src, (unsigned int)hdr->dst,
			   (int)payload_len, (unsigned int)hdr->flags);

		if (payload_len > 0) {
			DBG_PRINTF("RX payload:");
			for (uint16_t i = 0; i < payload_len; i++)
				DBG_PRINTF(" %02x", payload[i]);
			DBG_PRINTF("\r\n");

			/* echo 回去: src 作为 dst */
			if (rpmsg_sendto(vqid, hdr->src, hdr->data, payload_len))
				DBG_PRINTF("echo send failed, src=0x%x len=%d\r\n",
					   (unsigned int)hdr->src, (int)payload_len);
		}

		/* 告诉 host: 该 RX buffer 已处理完成 */
		vring_add_used(&vr_rx.vr, desc_idx, desc->len);

		/*
		 * 这里用 vqid,与 Linux 侧的 kick 语义保持一致:
		 *  - 通常 host 用 vqid=1 表示 RX vring
		 */
		msgbox_kick_host(vqid);
	}
}

/* 判断 host 侧 virtio 驱动是否已经设置 DRIVER_OK */
static int host_ready(void)
{
	return (resources.rpmsg_vdev.status & VIRTIO_CONFIG_S_DRIVER_OK);
}

int main(void)
{
	int      ns_sent;
	uint32_t vqid;
	int      i;
	int      vrings_synced;
	int      ready;

	ns_sent       = 0;
	i             = 0;
	vrings_synced = 0;

	DBG_PRINTF("C906 RPMsg firmware start\r\n");

	/* 根据 resource table 中的 DA 初始化 TX/RX vring */
	vring_setup(&vr_tx, (uintptr_t)VRING0_DA, VRING_NUM, VRING_ALIGN);
	vring_setup(&vr_rx, (uintptr_t)VRING1_DA, VRING_NUM, VRING_ALIGN);

	DBG_PRINTF("VRING0_DA=0x%08x, VRING1_DA=0x%08x\r\n",
		   (unsigned int)VRING0_DA, (unsigned int)VRING1_DA);

	/* 初始化 MSGBOX(打开 RX IRQ 等) */
	msgbox_init();

	while (1) {
		/* 每轮先检查 host 状态 */
		ready = host_ready();

		/* host 一旦进入 DRIVER_OK,就同步一次 vring 状态并发 NS */
		if (ready && !vrings_synced) {
			vr_rx.last_avail = vr_rx.vr.avail->idx;
			DBG_PRINTF("vring sync: avail_rx=%d avail_tx=%d\r\n",
				   (int)vr_rx.vr.avail->idx,
				   (int)vr_tx.vr.avail->idx);
			vrings_synced = 1;
		}

		if (ready && !ns_sent) {
			if (!rpmsg_send_ns()) {
				DBG_PRINTF("sent NS announcement\r\n");
				ns_sent = 1;
			}
		}

		/*
		 * 无论 host 是否 ready,都先把 MSGBOX 里的 kick 消耗掉,
		 * 避免 Linux 侧 MBOX_TX_QUEUE 堵死导致 "mbox kick failed: -105".
		 */
		if (msgbox_poll(&vqid)) {
			if (!ready) {
				/*
				 * host 还没把 virtio 状态切到 DRIVER_OK,
				 * 这些 kick 多半是早期握手/噪音,直接丢掉即可.
				 */
				DBG_PRINTF("host not ready, drop kick vqid=%d\r\n",
					   (int)vqid);
				continue;
			}

			/* 处理 host 在 RX vring 中放过来的消息,做 echo */
			process_host_messages(vqid);
		}

		/* i 可以在这里用于节流 debug,当前暂未使用 */
		(void)i;
	}
}
