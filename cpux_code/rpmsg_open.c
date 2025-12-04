// 保存为 /tmp/rpmsg_open.c
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s /dev/rpmsg_ctrl0 name dst\n", argv[0]); return 1; }
    struct rpmsg_endpoint_info ept = {0};
    strncpy(ept.name, argv[2], sizeof(ept.name) - 1);
    ept.src = RPMSG_ADDR_ANY;
    ept.dst = strtoul(argv[3], NULL, 0);
    int fd = open(argv[1], O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    if (ioctl(fd, RPMSG_CREATE_EPT_IOCTL, &ept) < 0) { perror("ioctl"); return 1; }
    return 0;
}
