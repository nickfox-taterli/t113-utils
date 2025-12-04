// Simple rpmsg ping: open endpoint, send payload, wait for reply
// Usage: ./rpmsg_ping /dev/rpmsg0 "hello"

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "Usage: %s /dev/rpmsgX \"message\"\n", argv[0]);
		return 1;
	}

	const char *dev = argv[1];
	const char *msg = argv[2];
	int fd = open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	ssize_t w = write(fd, msg, strlen(msg));
	if (w < 0) {
		perror("write");
		close(fd);
		return 1;
	}

	struct pollfd p = { .fd = fd, .events = POLLIN };
	int ret = poll(&p, 1, 30 * 1000); // 30s timeout
	if (ret < 0) {
		perror("poll");
		close(fd);
		return 1;
	} else if (ret == 0) {
		fprintf(stderr, "timeout waiting for reply\n");
		close(fd);
		return 2;
	}

	char buf[256] = {0};
	ssize_t r = read(fd, buf, sizeof(buf)-1);
	if (r < 0) {
		perror("read");
		close(fd);
		return 1;
	}

	printf("rx (%zd bytes): %s\n", r, buf);
	close(fd);
	return 0;
}
