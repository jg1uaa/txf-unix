// SPDX-License-Identifier: WTFPL

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>

#define MAGIC_SEND	0x53454e44	// "SEND"
#define MAGIC_RCVD	0x72637664	// "rcvd"
#define FILENAME_LEN	20
#define MIN_BLOCKSIZE	1
#define MAX_BLOCKSIZE	1024
#define MIN_TXDELAY	0
#define MAX_TXDELAY	60000		// msec
#define MAX_FILE_SIZE	0x7fffffff

extern char *optarg;

static char *serdev = NULL;
static bool rtscts = false;
static int blocksize = MAX_BLOCKSIZE;
static int txdelay = MIN_TXDELAY;

enum portmode {
	NONE, SERIAL, TCP_CLIENT, TCP_SERVER,
};
static enum portmode portmode = NONE;
static int portarg;

#define TCP_MAX_SOCKET 2

struct txf_header {
	uint32_t magic;
	uint32_t filesize;		// big endian
	char filename[FILENAME_LEN];
	char filename_term;		// must be zero
	char unused[3];
} __attribute__((packed));

struct txf_workingset {
	void *(*init)(char *arg);
	int (*process)(int d, void *handle);
	void (*finish)(void *handle);
};

struct txf_tx_workarea {
	FILE *fp;
	long size;
	struct txf_header h;
};

static ssize_t send_block(int d, void *buf, size_t size)
{
	size_t pos, wsize;

	for (pos = 0; pos < size; pos += wsize) {
		if ((wsize = write(d, buf + pos, size - pos)) < 0)
			break;
	}

	if (portmode == SERIAL) {
		// tcdrain(d);
		usleep(txdelay);
	}

	return pos;
}

static ssize_t recv_block(int d, void *buf, size_t size)
{
	size_t pos, rsize;

	for (pos = 0; pos < size; pos += rsize) {
		if ((rsize = read(d, buf + pos, size - pos)) < 0)
			break;
	}

	return pos;
}

static char *get_filename(char *filename)
{
#define DELIMITER	'/'
	
	int i, len;
	char *p;

	/* find the last delimiter character */
	len = strlen(filename);
	for (i = len - 1; i >= 0; i--) {
		if (filename[i] == DELIMITER)
			break;
	}

	/* filename starts after delimiter */
	p = filename + i + 1;

	/* check file name length */
	len = strlen(p);
	return (len < 1 || len > FILENAME_LEN) ? NULL : p;
}

static void *rx_init(char *arg)
{
	/* do nothing */
	return rx_init;
}

static int rx_process(int fd, void *handle)
{
	FILE *fp;
	int i, size, remain;
	struct txf_header h;
	char *fn, buf[MAX_BLOCKSIZE];
	int rv = -1;

	/* receive header */
	if (recv_block(fd, &h, sizeof(h)) < sizeof(h)) {
		printf("rx_process: recv_block (header)\n");
		goto fin0;
	}

	if (ntohl(h.magic) != MAGIC_SEND) {
		printf("rx_process: invalid header\n");
		goto fin0;
	}

	h.filename_term = '\0';
	size = ntohl(h.filesize);
	if ((fn = get_filename(h.filename)) == NULL) {
		printf("rx_process: invalid file name\n");
		goto fin0;
	}

	printf("%s, %d byte\n", fn, size);

	/* receive file */
	if ((fp = fopen(fn, "w")) == NULL) {
		printf("rx_process: fopen\n");
		goto fin0;
	}

	for (i = 0; i < size; i += blocksize) {
		remain = size - i;
		if (remain > blocksize)
			remain = blocksize;

		if (recv_block(fd, buf, remain) < remain) {
			printf("rx_process: recv_block (data)\n");
			goto fin1;
		}

		if (fwrite(buf, remain, 1, fp) < 1) {
			printf("rx_process: fwrite\n");
			goto fin1;
		}
	}

	/* send ack */
	h.magic = htonl(MAGIC_RCVD);
	if (send_block(fd, &h, sizeof(h)) < sizeof(h)) {
		printf("rx_process: send_block (ack)\n");
		goto fin1;
	}

	if (portmode == SERIAL) {
		tcdrain(fd);
		usleep(100000); // paranoia?
	}

	rv = 0;
fin1:
	fclose(fp);
fin0:
	return rv;
}

static void rx_finish(void *handle)
{
	/* do nothing */
}

static void *tx_init(char *filename)
{
	struct  txf_tx_workarea *wk;
	FILE *fp;
	long size;
	char *fn;

	wk = malloc(sizeof(*wk));
	if (wk == NULL) {
		printf("tx_init: malloc\n");
		goto fin0;
	}

	/* file open */
	if ((fn = get_filename(filename)) == NULL) {
		printf("tx_init: invalid file name\n");
		goto fin1;
	}

	if ((fp = fopen(filename, "r")) == NULL) {
		printf("tx_init: fopen\n");
		goto fin1;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size < 0 || size > MAX_FILE_SIZE) {
		printf("tx_init: invalid file size\n");
		goto fin2;
	}

	/* store file information to workarea */
	wk->fp = fp;
	wk->size = size;

	memset(&wk->h, 0, sizeof(wk->h));
	wk->h.magic = htonl(MAGIC_SEND);
	wk->h.filesize = htonl(size);
	strcpy(wk->h.filename, fn);

	printf("%s, %ld byte\n", fn, size);
	goto fin0;

fin2:
	fclose(fp);
fin1:
	free(wk);
	wk = NULL;
fin0:
	return wk;
}

static int tx_process(int d, void *handle)
{
	struct  txf_tx_workarea *wk = handle;
	long i, remain;
	struct txf_header h;
	char buf[MAX_BLOCKSIZE];
	int rv = -1;

	/* send header */
	if (send_block(d, &wk->h, sizeof(wk->h)) < sizeof(wk->h)) {
		printf("tx_process: send_block (header)\n");
		goto fin0;
	}

	/* send file */
	for (i = 0; i < wk->size; i += blocksize) {
		remain = wk->size - i;
		if (remain > blocksize)
			remain = blocksize;

		if (fread(buf, remain, 1, wk->fp) < 1) {
			printf("tx_process: fread\n");
			goto fin0;
		}

		if (send_block(d, buf, remain) < remain) {
			printf("tx_process: send_block (data)\n");
			goto fin0;
		}
	}

	/* receive ack */
	if (recv_block(d, &h, sizeof(h)) < sizeof(h)) {
		printf("tx_process: recv_block (ack)\n");
		goto fin0;
	}

	if (ntohl(h.magic) != MAGIC_RCVD) {
		printf("tx_process: invalid ack\n");
		goto fin0;
	}

	rv = 0;
fin0:
	return rv;
}

static void tx_finish(void *handle)
{
	struct txf_tx_workarea *wk = handle;

	fclose(wk->fp);
	free(handle);
}

static int xfer(int fd, char *arg, struct txf_workingset *work)
{
	void *handle;
	int rv = -1;

	if ((handle = (*work->init)(arg)) == NULL) {
		printf("xfer: init\n");
		goto fin0;
	}

	if ((*work->process)(fd, handle)) {
		printf("xfer: process\n");
		goto fin1;
	}

	rv = 0;
fin1:
	(*work->finish)(handle);
fin0:
	return rv;
}

static int get_speed(int speed)
{
#if defined(B38400) && (B38400 == 38400)
	return speed;
#else
	switch (speed) {
	case 0:		return B0;
	case 50:	return B50;
	case 75:	return B75;
	case 110:	return B110;
	case 134:	return B134;
	case 150:	return B150;
	case 200:	return B200;
	case 300:	return B300;
	case 600:	return B600;
	case 1200:	return B1200;
	case 1800:	return B1800;
	case 2400:	return B2400;
	case 4800:	return B4800;
	case 9600:	return B9600;
	case 19200:	return B19200;
	case 38400:	return B38400;
#if defined(B57600)
	case 57600:	return B57600;
#endif
#if defined(B115200)
	case 115200:	return B115200;
#endif
#if defined(B230400)
	case 230400:	return B230400;
#endif
#if defined(B460800)
	case 460800:	return B460800;
#endif
#if defined(B500000)
	case 500000:	return B500000;
#endif
#if defined(B576000)
	case 576000:	return B576000;
#endif
#if defined(B921600)
	case 921600:	return B921600;
#endif
#if defined(B1000000)
	case 1000000:	return B1000000;
#endif
#if defined(B1152000)
	case 1152000:	return B1152000;
#endif
#if defined(B1500000)
	case 1500000:	return B1500000;
#endif
#if defined(B2000000)
	case 2000000:	return B2000000;
#endif
#if defined(B2500000)
	case 2500000:	return B2500000;
#endif
#if defined(B3000000)
	case 3000000:	return B3000000;
#endif
#if defined(B3500000)
	case 3500000:	return B3500000;
#endif
#if defined(B4000000)
	case 4000000:	return B4000000;
#endif
	default:	return -1;
	}
#endif
}

static bool set_nonblock(int d, bool nonblock)
{
	int flags;

	return ((flags = fcntl(d, F_GETFL)) < 0 ||
		fcntl(d, F_SETFL, nonblock ?
		      (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) < 0);
}

static void wait_for_stable(int fd)
{
	char buf;
	int i;

	if (set_nonblock(fd, true))
		return;

	/* discard garbage (silent 10msec required) */
	for (i = 0; i < 10; ) {
		if (read(fd, &buf, sizeof(buf)) == -1) {
			if (errno == EAGAIN) {
				usleep(1000);
				i++;
			}
		} else {
			i = 0;
		}
	}
}

static int open_serial(void)
{
	int fd;
	struct termios t;

	if ((fd = open(serdev,
		       O_RDWR | O_NOCTTY | O_EXCL | O_NONBLOCK)) < 0)
		goto fin0;

	memset(&t, 0, sizeof(t));
	cfsetospeed(&t, get_speed(portarg));
	cfsetispeed(&t, get_speed(portarg));

	t.c_cflag |= CREAD | CLOCAL | CS8;
	if (rtscts) t.c_cflag |= CRTSCTS;
	t.c_iflag = INPCK;
	t.c_oflag = 0;
	t.c_lflag = 0;
	t.c_cc[VTIME] = 0;
	t.c_cc[VMIN] = 1;

	tcflush(fd, TCIOFLUSH);
	tcsetattr(fd, TCSANOW, &t);

	wait_for_stable(fd);
	if (set_nonblock(fd, false))
		goto fin1;

	goto fin0;

fin1:
	close(fd);
	fd = -1;
fin0:
	return fd;
}

static const char *inet_ntopXX(int af, const void *src, char *dst, socklen_t size)
{
	struct sockaddr_in *s4 = (struct sockaddr_in *)src;
	struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)src;

	switch (af) {
	case AF_INET:
		return inet_ntop(af, &s4->sin_addr.s_addr, dst, size);
	case AF_INET6:
		return inet_ntop(af, &s6->sin6_addr.s6_addr, dst, size);
	default:
		return strncpy(dst, "unknown", size);
	}
}

static struct addrinfo *acquire_address_info(void)
{
	struct addrinfo hints, *res;
	char tmp[16];

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_socktype = SOCK_STREAM;
	snprintf(tmp, sizeof(tmp), "%d", portarg);

	return getaddrinfo(serdev, tmp, &hints, &res) ? NULL : res;
}

static int wait_for_accept(int *list, int entries)
{
	int i, s = -1;
	struct pollfd *pfd;

	if (entries <= 0 ||
	    (pfd = calloc(sizeof(struct pollfd), entries)) == NULL)
		goto fin0;

	for (i = 0; i < entries; i++) {
		pfd[i].fd = list[i];
		pfd[i].events = POLLIN;
	}

	if (poll(pfd, entries, -1) <= 0)
		goto fin1;

	for (i = 0; i < entries; i++) {
		if (pfd[i].revents & POLLIN) {
			s = list[i];
			break;
		}
	}

fin1:
	free(pfd);
fin0:
	return s;
}

static int open_tcp_server(void)
{
	int i, s, enable = 1, fd = -1;
	int sock[TCP_MAX_SOCKET], numsock;
	struct addrinfo *res, *res0;
	struct sockaddr_storage ss;
	socklen_t ss_len;
	char addr_str[INET6_ADDRSTRLEN];

	if ((res0 = acquire_address_info()) == NULL)
		goto fin0;

	numsock = 0;
	for (res = res0; res && numsock < TCP_MAX_SOCKET;
	     res = res->ai_next) {
		if ((s = socket(res->ai_family, res->ai_socktype,
				res->ai_protocol)) < 0)
			continue;

		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
			       &enable, sizeof(enable)) >= 0 &&
		    bind(s, res->ai_addr, res->ai_addrlen) >= 0 &&
		    listen(s, 1) >= 0 && !set_nonblock(s, true)) {
			sock[numsock++] = s;
			continue;
		}

		close(s);
	}

	while (1) {
		if ((s = wait_for_accept(sock, numsock)) < 0)
			break;

		ss_len = sizeof(ss);
		if ((fd = accept(s, (struct sockaddr *)&ss, &ss_len)) < 0)
			continue;

		/* nonblock is inherited from original socket (OpenBSD) */
		if (set_nonblock(fd, false)) {
			fd = s = -1;
			break;
		}

		inet_ntopXX(ss.ss_family, &ss, addr_str, sizeof(addr_str));
		printf("connected from %s\n", addr_str);
		break;
	}

	for (i = 0; i < numsock; i++)
		close(sock[i]);

	freeaddrinfo(res0);
fin0:
	return fd;
}

static int open_tcp_client(void)
{
	int s = -1;
	struct addrinfo *res, *res0;
	char addr_str[INET6_ADDRSTRLEN];

	if ((res0 = acquire_address_info()) == NULL)
		goto fin0;

	for (res = res0; res; res = res->ai_next) {
		if ((s = socket(res->ai_family, res->ai_socktype,
				 res->ai_protocol)) < 0)
			continue;

		if (connect(s, res->ai_addr, res->ai_addrlen) >= 0) {
			inet_ntopXX(res->ai_family, res->ai_addr,
				    addr_str, sizeof(addr_str));
			printf("connected to %s\n", addr_str);
			break;
		}

		close(s);
		s = -1;
	}

	freeaddrinfo(res0);
fin0:
	return s;
}

static int do_main(char *tx_file)
{
	int fd_ser, ret = -1;
	struct txf_workingset rx_set = {rx_init, rx_process, rx_finish};
	struct txf_workingset tx_set = {tx_init, tx_process, tx_finish};
	struct txf_workingset *set;

	if (tx_file == NULL) {
		printf("* receive\n");
		set = &rx_set;
	} else {
		printf("* transmit\n");
		set = &tx_set;
	}

	switch (portmode) {
	case SERIAL:
		fd_ser = open_serial();
		break;
	case TCP_CLIENT:
		fd_ser = open_tcp_client();
		break;
	case TCP_SERVER:
		fd_ser = open_tcp_server();
		break;
	default:
		fd_ser = -1;
		break;
	}
	if (fd_ser < 0) {
		printf("device open error\n");
		goto fin0;
	}

	xfer(fd_ser, tx_file, set);
	ret = 0;

	close(fd_ser);
fin0:
	return ret;
}

int main(int argc, char *argv[])
{
	int ch, v;
	char *tx_file = NULL;

	while ((ch = getopt(argc, argv, "s:p:P:l:cf:b:w:")) != -1) {
		switch (ch) {
		case 's':
			portmode = SERIAL;
			portarg = atoi(optarg);
			break;
		case 'p':
			portmode = TCP_CLIENT;
			portarg = atoi(optarg);
			break;
		case 'P':
			portmode = TCP_SERVER;
			portarg = atoi(optarg);
			break;
		case 'l':
			serdev = optarg;
			break;
		case 'c':
			rtscts = true;
			break;
		case 'f':
			tx_file = optarg;
			break;
		case 'b':
			v = atoi(optarg);
			if (v < MIN_BLOCKSIZE) v = MIN_BLOCKSIZE;
			if (v > MAX_BLOCKSIZE) v = MAX_BLOCKSIZE;
			blocksize = v;
			break;
		case 'w':
			v = atoi(optarg);
			if (v < MIN_TXDELAY) v = MIN_TXDELAY;
			if (v > MAX_TXDELAY) v = MAX_TXDELAY;
			txdelay = v * 1000;
			break;
		}
	}

	if (serdev == NULL || portmode == NONE ||
	    (portmode == SERIAL && get_speed(portarg) < 0)) {
		printf("usage:	%s -p [client port] -l [IP address]\n",
		       argv[0]);
		printf("	%s -P [server port] -l [IP address] "
		       " -f [filename]\n", argv[0]);
		goto fin0;
	}

	do_main(tx_file);

fin0:
	return 0;
}
