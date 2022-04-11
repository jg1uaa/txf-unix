// SPDX-License-Identifier: WTFPL

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define MAGIC_SEND	0x53454e44	// "SEND"
#define MAGIC_RCVD	0x72637664	// "rcvd"
#define FILENAME_LEN	20
#define BLOCKSIZE	1024
#define MAX_FILE_SIZE	0x7fffffff

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
	/* do noting */
	return rx_init;
}

static int rx_process(int fd, void *handle)
{
	FILE *fp;
	int i, size, remain;
	struct txf_header h;
	char *fn, buf[BLOCKSIZE];
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

	for (i = 0; i < size; i += BLOCKSIZE) {
		remain = size - i;
		if (remain > BLOCKSIZE)
			remain = BLOCKSIZE;

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
	char buf[BLOCKSIZE];
	int rv = -1;

	/* send header */
	if (send_block(d, &wk->h, sizeof(wk->h)) < sizeof(wk->h)) {
		printf("tx_process: send_block (header)\n");
		goto fin0;
	}

	/* send file */
	for (i = 0; i < wk->size; i += BLOCKSIZE) {
		remain = wk->size - i;
		if (remain > BLOCKSIZE)
			remain = BLOCKSIZE;

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
	struct  txf_tx_workarea *wk = handle;

	fclose(wk->fp);
	free(handle);
}

static int client(int fd, struct sockaddr_in *addr, char *arg, struct txf_workingset *work)
{
	void *handle;
	int rv = -1;

	printf("* client\n");

	if ((handle = (*work->init)(arg)) == NULL) {
		printf("client: init\n");
		goto fin0;
	}

	/* connect to server */
	if (connect(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
		printf("client: connect\n");
		goto fin1;
	}

	printf("connected to %s\n", inet_ntoa(addr->sin_addr));

	if ((*work->process)(fd, handle)) {
		printf("client: process\n");
		goto fin1;
	}

	rv = 0;
fin1:
	(*work->finish)(handle);
fin0:
	return rv;
}

static int server(int fd, struct sockaddr_in *addr, char *arg, struct txf_workingset *work)
{
	void *handle;
	int d, rv = -1;
	struct sockaddr_in peer;
	socklen_t peer_len;

	printf("* server\n");

	if ((handle = (*work->init)(arg)) == NULL) {
		printf("server: init\n");
		goto fin0;
	}
	
	/* wait for connect */
	if (bind(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
		printf("server: bind\n");
		goto fin1;
	}

	if (listen(fd, 1) < 0) {
		printf("server: listen\n");
		goto fin1;
	}

	peer_len = sizeof(peer);
	if ((d = accept(fd, (struct sockaddr *)&peer, &peer_len)) < 0) {
		printf("server: accept\n");
		goto fin1;
	}

	printf("connected from %s\n", inet_ntoa(peer.sin_addr));

	if ((*work->process)(d, handle)) {
		printf("server: process\n");
		goto fin2;
	}

	rv = 0;
fin2:
	close(d);
fin1:
	(*work->finish)(handle);
fin0:
	return rv;
}

int main(int argc, char *argv[])
{
	int fd, tx_file, rx_server, port;
	struct sockaddr_in addr;
	struct txf_workingset rx_set = {rx_init, rx_process, rx_finish};
	struct txf_workingset tx_set = {tx_init, tx_process, tx_finish};

	if (argc < 3 || argc > 4) {
		printf("%s [ipv4-addr] [port] [(filename to send)]\n",
		       argv[0]);
		goto fin0;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("main: socket\n");
		goto fin0;
	}

	/* default: tx-server/rx-client mode */
	port = atoi(argv[2]);
	tx_file = (argc == 4) ? 1 : 0;
	rx_server = 0;

	/* if port is negative, rx-server/tx-client mode */
	if (*argv[2] == '-') {
		port = -port;
		tx_file ^= 1;
		rx_server = 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons(port);

	switch ((rx_server << 1) | tx_file) {
	case 0:
		client(fd, &addr, NULL, &rx_set);
		break;
	case 1:
		server(fd, &addr, argv[3], &tx_set);
		break;
	case 2:
		client(fd, &addr, argv[3], &tx_set);
		break;
	case 3:
		server(fd, &addr, NULL, &rx_set);
		break;
	}

	close(fd);
fin0:
	return 0;
}
