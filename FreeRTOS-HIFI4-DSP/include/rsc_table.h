#ifndef __HIFI4_RSC_TABLE_H__
#define __HIFI4_RSC_TABLE_H__

#include <stdint.h>

#define RSC_CARVEOUT    0
#define RSC_DEVMEM      1
#define RSC_TRACE       2
#define RSC_VDEV        3
#define RSC_LAST        6

#define VIRTIO_ID_RPMSG 7
#define VIRTIO_RPMSG_F_NS 0

struct fw_rsc_hdr {
	uint32_t type;
} __attribute__((packed));

struct resource_table {
	uint32_t ver;
	uint32_t num;
	uint32_t reserved[2];
	uint32_t offset[1];
} __attribute__((packed));

struct fw_rsc_vdev_vring {
	uint32_t da;
	uint32_t align;
	uint32_t num;
	uint32_t notifyid;
	uint32_t pa;
} __attribute__((packed));

struct fw_rsc_vdev {
	uint32_t id;
	uint32_t notifyid;
	uint32_t dfeatures;
	uint32_t gfeatures;
	uint32_t config_len;
	uint8_t status;
	uint8_t num_of_vrings;
	uint8_t reserved[2];
} __attribute__((packed));

struct my_resource_table {
	struct resource_table base;
	struct fw_rsc_hdr rpmsg_hdr;
	struct fw_rsc_vdev rpmsg_vdev;
	struct fw_rsc_vdev_vring vring[2];
} __attribute__((packed));

extern struct my_resource_table resources;

#endif /* __HIFI4_RSC_TABLE_H__ */
