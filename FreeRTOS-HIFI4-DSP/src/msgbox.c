/* 本工程结构模仿C906同理,所以注释懒得写了. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"
#include "rpmsg.h"
#include "rsc_table.h"

#ifdef HIFI4_RPMSG_DEBUG
#define DBG_PRINTF printf
#else
#define DBG_PRINTF(...) do { if (0) printf(__VA_ARGS__); } while (0)
#endif

#define MSGBOX_BASE_LOCAL	SUNXI_MSGBOX_DSP_BASE
#define MSGBOX_BASE_REMOTE	SUNXI_MSGBOX_ARM_BASE
#define MSGBOX_REGION_SIZE	0x1000U

#define SUNXI_MSGBOX_OFFSET(n)			(0x100 * (n))
#define SUNXI_MSGBOX_READ_IRQ_ENABLE(n)	(0x20 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_READ_IRQ_STATUS(n)	(0x24 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_ENABLE(n)	(0x30 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_WRITE_IRQ_STATUS(n)	(0x34 + SUNXI_MSGBOX_OFFSET(n))
#define SUNXI_MSGBOX_MSG_STATUS(n, p)		(0x60 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))
#define SUNXI_MSGBOX_MSG_FIFO(n, p)		(0x70 + SUNXI_MSGBOX_OFFSET(n) + 0x4 * (p))

#define RD_IRQ_EN_MASK		0x1
#define RD_IRQ_EN_SHIFT(p)	((p) * 2)

#define RD_IRQ_PEND_MASK	0x1
#define RD_IRQ_PEND_SHIFT(p)	((p) * 2)

#define MSG_NUM_MASK		0xF
#define MSG_NUM_SHIFT		0

#define LOCAL_N	0
#define REMOTE_N	0
#define CHAN_P		0

#define VRING_ALIGN	4096U
#define VRING_NUM	16U

#define VRING0_DA	(resources.vring[0].da)
#define VRING1_DA	(resources.vring[1].da)

#define SHM_BASE_ADDR	  0x41100000U
#define SHM_SIZE_BYTES	 0x00100000U
#define SHM_LIMIT_ADDR	 (SHM_BASE_ADDR + SHM_SIZE_BYTES)

#define VIRTIO_CONFIG_S_DRIVER_OK	0x04

#define LOCAL_EPT_ADDR	0x2
#define RPMSG_ECHO_NAME	"hifi4-echo"

struct vring_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
} __attribute__((packed));

struct vring_avail {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[VRING_NUM];
} __attribute__((packed));

struct vring_used_elem {
	uint32_t id;
	uint32_t len;
} __attribute__((packed));

struct vring_used {
	uint16_t		   flags;
	uint16_t		   idx;
	struct vring_used_elem ring[VRING_NUM];
} __attribute__((packed));

struct vring {
	struct vring_desc	*desc;
	struct vring_avail	*avail;
	struct vring_used	*used;
	uint16_t		 num;
};

struct vr_ctrl {
	struct vring vr;
	uint16_t     last_avail;
};

static struct vr_ctrl vr_tx;
static struct vr_ctrl vr_rx;

static int msgbox_addr_valid(uint32_t addr)
{
	if ((addr >= MSGBOX_BASE_LOCAL &&
	     addr < MSGBOX_BASE_LOCAL + MSGBOX_REGION_SIZE) ||
	    (addr >= MSGBOX_BASE_REMOTE &&
	     addr < MSGBOX_BASE_REMOTE + MSGBOX_REGION_SIZE))
		return 1;

	DBG_PRINTF("MSGBOX: invalid addr 0x%08x\n", addr);
	return 0;
}

static inline void mb_write(uint32_t base, uint32_t off, uint32_t val)
{
	uint32_t addr = base + off;

	if (!msgbox_addr_valid(addr))
		return;

	writel(addr, val);
}

static inline uint32_t mb_read(uint32_t base, uint32_t off)
{
	uint32_t addr = base + off;

	if (!msgbox_addr_valid(addr))
		return 0;

	return readl(addr);
}

static void msgbox_init(void)
{
	uint32_t val;

	val = mb_read(MSGBOX_BASE_LOCAL,
		      SUNXI_MSGBOX_READ_IRQ_ENABLE(LOCAL_N));
	val |= (RD_IRQ_EN_MASK << RD_IRQ_EN_SHIFT(CHAN_P));
	mb_write(MSGBOX_BASE_LOCAL,
		 SUNXI_MSGBOX_READ_IRQ_ENABLE(LOCAL_N), val);

	mb_write(MSGBOX_BASE_LOCAL,
		 SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N),
		 RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P));
}

static int msgbox_poll(uint32_t *vqid)
{
	uint32_t pend;

	pend = mb_read(MSGBOX_BASE_LOCAL, SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N));
    if (!(pend & (RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P)))) {
		// 需要同样的魔法,参考C906
		mb_read(MSGBOX_BASE_LOCAL, SUNXI_MSGBOX_MSG_FIFO(LOCAL_N, CHAN_P));
        return 0;
    }

    *vqid = mb_read(MSGBOX_BASE_LOCAL,
                        SUNXI_MSGBOX_MSG_FIFO(LOCAL_N, CHAN_P));

    mb_write(MSGBOX_BASE_LOCAL,
             SUNXI_MSGBOX_READ_IRQ_STATUS(LOCAL_N),
             RD_IRQ_PEND_MASK << RD_IRQ_PEND_SHIFT(CHAN_P));

    return 1;
}


static void msgbox_kick_host(uint32_t vqid)
{
	uint32_t used;

	used = mb_read(MSGBOX_BASE_REMOTE,
		      SUNXI_MSGBOX_MSG_STATUS(REMOTE_N, CHAN_P));
	used = (used >> MSG_NUM_SHIFT) & MSG_NUM_MASK;
	if (used >= 8)
		return;

	mb_write(MSGBOX_BASE_REMOTE,
		 SUNXI_MSGBOX_MSG_FIFO(REMOTE_N, CHAN_P), vqid);
}

static void vring_setup(struct vr_ctrl *vc, uintptr_t base,
			uint16_t num, uint32_t align)
{
	uintptr_t avail;
	uintptr_t used;

	vc->vr.desc = (struct vring_desc *)base;
	avail = base + sizeof(struct vring_desc) * num;
	vc->vr.avail = (struct vring_avail *)avail;

	used = (uintptr_t)&vc->vr.avail->ring[num];
	used = (used + align - 1) & ~(uintptr_t)(align - 1);
	vc->vr.used = (struct vring_used *)used;

	vc->vr.num = num;
	vc->last_avail = 0;
}

static int vring_get_avail(struct vr_ctrl *vc, uint16_t *desc_idx)
{
	uint16_t idx = vc->vr.avail->idx;

	if (vc->last_avail == idx)
		return -1;

	*desc_idx = vc->vr.avail->ring[vc->last_avail % vc->vr.num];
	vc->last_avail++;
	return 0;
}

static void vring_add_used(struct vring *vr, uint16_t id, uint32_t len)
{
	uint16_t used_idx = vr->used->idx;

	vr->used->ring[used_idx % vr->num].id = id;
	vr->used->ring[used_idx % vr->num].len = len;

	__sync_synchronize();

	vr->used->idx = used_idx + 1;
}

static int rpmsg_sendto(uint32_t vqid, uint32_t dst,
			const void *data, uint16_t len)
{
	uint16_t desc_idx;
	struct vring_desc *desc;
	struct rpmsg_hdr *hdr;

	if (vring_get_avail(&vr_tx, &desc_idx))
		return -1;

	desc = &vr_tx.vr.desc[desc_idx];
	hdr = (struct rpmsg_hdr *)(uintptr_t)desc->addr;

	if (len + sizeof(*hdr) > desc->len)
		len = desc->len - sizeof(*hdr);

	hdr->src = LOCAL_EPT_ADDR;
	hdr->dst = dst;
	hdr->reserved = 0;
	hdr->len = len;
	hdr->flags = 0;

	memcpy(hdr->data, data, len);

	vring_add_used(&vr_tx.vr, desc_idx, len + sizeof(*hdr));
	msgbox_kick_host(vqid);
	return 0;
}

static int rpmsg_send_ns(void)
{
	struct rpmsg_ns_msg ns;
	int ret;

	memset(&ns, 0, sizeof(ns));
	strncpy(ns.name, RPMSG_ECHO_NAME, RPMSG_NAME_SIZE - 1);
	ns.addr = LOCAL_EPT_ADDR;
	ns.flags = RPMSG_NS_CREATE;

	ret = rpmsg_sendto(0, RPMSG_NS_ADDR, &ns, sizeof(ns));
	if (ret)
		DBG_PRINTF("rpmsg_send_ns failed (%d)\n", ret);

	return ret ? -1 : 0;
}

static void process_host_messages(uint32_t vqid)
{
	uint16_t desc_idx;
	struct vring_desc *desc;
	struct rpmsg_hdr *hdr;
	uint32_t addr_lo;
	uint32_t addr_hi;
	uint64_t addr;
	uint16_t payload_len;
	uint16_t max_payload;
	const uint8_t *payload;
	static int rx_invalid_warned;

	while (vring_get_avail(&vr_rx, &desc_idx) == 0) {
		desc = &vr_rx.vr.desc[desc_idx];
		hdr = (struct rpmsg_hdr *)(uintptr_t)desc->addr;
		payload = hdr->data;

		addr = desc->addr;
		addr_lo = (uint32_t)(addr & 0xffffffffu);
		addr_hi = (uint32_t)((addr >> 32) & 0xffffffffu);

		if (addr < SHM_BASE_ADDR || addr >= SHM_LIMIT_ADDR) {
			if (!rx_invalid_warned) {
				DBG_PRINTF("RX invalid buf: idx=%d addr=0x%08x%08x len=%d\n",
					   (int)desc_idx, addr_hi, addr_lo,
					   (int)desc->len);
				DBG_PRINTF("RX state: avail_idx=%d last_avail=%d\n",
					   (int)vr_rx.vr.avail->idx,
					   (int)vr_rx.last_avail);
				rx_invalid_warned = 1;
			}
			break;
		}

		payload_len = hdr->len;
		max_payload = 0;
		if (desc->len > sizeof(*hdr))
			max_payload = desc->len - sizeof(*hdr);
		if (payload_len > max_payload)
			payload_len = max_payload;

		if (payload_len > 0)
			rpmsg_sendto(vqid, hdr->src, payload, payload_len);

		vring_add_used(&vr_rx.vr, desc_idx, desc->len);
		msgbox_kick_host(vqid);
	}
}

static int host_ready(void)
{
	return (resources.rpmsg_vdev.status & VIRTIO_CONFIG_S_DRIVER_OK);
}

void rpmsg_service_run(void)
{
	int ns_sent = 0;
	int vrings_synced = 0;
	uint32_t vqid;
	int ready;

	DBG_PRINTF("HiFi4 RPMsg service start\n");

	vring_setup(&vr_tx, (uintptr_t)VRING0_DA, VRING_NUM, VRING_ALIGN);
	vring_setup(&vr_rx, (uintptr_t)VRING1_DA, VRING_NUM, VRING_ALIGN);

	msgbox_init();

	DBG_PRINTF("MSGBOX_BASE_LOCAL=0x%08x LOCAL_N=%d REMOTE_N=%d CHAN_P=%d\r\n",
           (unsigned)MSGBOX_BASE_LOCAL, LOCAL_N, REMOTE_N, CHAN_P);

	while (1) {
		ready = host_ready();

		if (ready && !vrings_synced) {
			vr_rx.last_avail = vr_rx.vr.avail->idx;
			DBG_PRINTF("vring sync: avail_rx=%d avail_tx=%d\n",
				   (int)vr_rx.vr.avail->idx,
				   (int)vr_tx.vr.avail->idx);
			vrings_synced = 1;
		}

		if (ready && !ns_sent) {
			if (!rpmsg_send_ns()) {
				DBG_PRINTF("sent NS announcement\n");
				ns_sent = 1;
			}
		}

		if (msgbox_poll(&vqid)) {
			if (!ready) {
				DBG_PRINTF("host not ready, drop kick vqid=%d\n",
					   (int)vqid);
				continue;
			}
			process_host_messages(vqid);
		}
	}
}
