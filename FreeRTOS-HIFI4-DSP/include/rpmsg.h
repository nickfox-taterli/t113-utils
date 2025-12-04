#ifndef __HIFI4_RPMSG_H__
#define __HIFI4_RPMSG_H__

#include <stdint.h>

#define RPMSG_NAME_SIZE    32
#define RPMSG_NS_ADDR      53
#define RPMSG_NS_CREATE    0
#define RPMSG_NS_DESTROY   1
#define RPMSG_HDR_FLAG_NS  1

struct rpmsg_hdr {
	uint32_t src;
	uint32_t dst;
	uint32_t reserved;
	uint16_t len;
	uint16_t flags;
	uint8_t data[0];
} __attribute__((packed));

struct rpmsg_ns_msg {
	char name[RPMSG_NAME_SIZE];
	uint32_t addr;
	uint32_t flags;
} __attribute__((packed));

#endif /* __HIFI4_RPMSG_H__ */
