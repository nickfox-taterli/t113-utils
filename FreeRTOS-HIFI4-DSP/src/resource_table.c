#include <stddef.h>
#include <stdint.h>

#include "rsc_table.h"

#define VRING_ALIGN	4096
#define VRING_NUM	16

#define HIFI4_SHM_BASE	0x41100000UL
#define VRING0_DA	(HIFI4_SHM_BASE + 0x00010000UL)
#define VRING1_DA	(HIFI4_SHM_BASE + 0x00012000UL)

struct my_resource_table resources __attribute__((section(".resource_table"))) = {
	.base = {
		.ver = 1,
		.num = 1,
		.reserved = { 0, 0 },
		.offset = { offsetof(struct my_resource_table, rpmsg_hdr) },
	},
	.rpmsg_hdr = {
		.type = RSC_VDEV,
	},
	.rpmsg_vdev = {
		.id = VIRTIO_ID_RPMSG,
		.notifyid = 0,
		.dfeatures = (1 << VIRTIO_RPMSG_F_NS),
		.gfeatures = 0,
		.config_len = 0,
		.status = 0,
		.num_of_vrings = 2,
		.reserved = { 0, 0 },
	},
	.vring = {
		{
			.da = VRING0_DA,
			.align = VRING_ALIGN,
			.num = VRING_NUM,
			.notifyid = 0,
			.pa = 0,
		},
		{
			.da = VRING1_DA,
			.align = VRING_ALIGN,
			.num = VRING_NUM,
			.notifyid = 1,
			.pa = 0,
		},
	},
};
