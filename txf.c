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

static int client(int fd, struct sockaddr_in *addr)
{
	FILE *fp;
	int i, size, remain;
	struct txf_header h;
	char buf[BLOCKSIZE];

	printf("* client\n");

	/* connect to server */
	if (connect(fd, (struct sockaddr *)addr, sizeof(*addr)) < 0) {
		printf("client: connect\n");
		goto fin0;
	}

	printf("connected to %s\n", inet_ntoa(addr->sin_addr));

	/* receive header */
	if (recv_block(fd, &h, sizeof(h)) < sizeof(h)) {
		printf("client: recv_block (header)\n");
		goto fin0;
	}

	if (ntohl(h.magic) != MAGIC_SEND) {
		printf("client: invalid header\n");
		goto fin0;
	}

	h.filename_term = '\0';
	size = ntohl(h.filesize);

	printf("%s, %d byte\n", h.filename, size);

	/* receive file */
	if ((fp = fopen(h.filename, "w")) == NULL) {
		printf("client: fopen\n");
		goto fin0;
	}

	for (i = 0; i < size; i += BLOCKSIZE) {
		remain = size - i;
		if (remain > BLOCKSIZE)
			remain = BLOCKSIZE;

		if (recv_block(fd, buf, remain) < remain) {
			printf("client: recv_block (data)\n");
			goto fin1;
		}

		if (fwrite(buf, remain, 1, fp) < 1) {
			printf("client: fwrite\n");
			goto fin1;
		}
	}

	/* send ack */
	h.magic = htonl(MAGIC_RCVD);
	if (send_block(fd, &h, sizeof(h)) < sizeof(h)) {
		printf("client: send_block (ack)\n");
		goto fin1;
	}

fin1:
	fclose(fp);
fin0:
	return 0;
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

static int server(int fd, struct sockaddr_in *addr, char *filename)
{
	int d;
	FILE *fp;
	long i, size, remain;
	char *fn;
	char buf[BLOCKSIZE];
	struct txf_header h;
	struct sockaddr_in peer;
	socklen_t peer_len;

	printf("* server\n");

	/* file open */
	if ((fn = get_filename(filename)) == NULL) {
		printf("server: invalid file name\n");
		goto fin0;
	}

	if ((fp = fopen(filename, "r")) == NULL) {
		printf("server: fopen\n");
		goto fin0;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size < 0 || size > MAX_FILE_SIZE) {
		printf("server: invalid file size\n");
		goto fin1;
	}

	printf("%s, %ld byte\n", fn, size);

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

	/* send header */
	memset(&h, 0, sizeof(h));
	h.magic = htonl(MAGIC_SEND);
	h.filesize = htonl(size);
	strcpy(h.filename, fn);

	if (send_block(d, &h, sizeof(h)) < sizeof(h)) {
		printf("server: send_block (header)\n");
		goto fin2;
	}

	/* send file */
	for (i = 0; i < size; i += BLOCKSIZE) {
		remain = size - i;
		if (remain > BLOCKSIZE)
			remain = BLOCKSIZE;

		if (fread(buf, remain, 1, fp) < 1) {
			printf("server: fread\n");
			goto fin2;
		}

		if (send_block(d, buf, remain) < remain) {
			printf("server: send_block (data)\n");
			goto fin2;
		}
	}

	/* receive ack */
	if (recv_block(d, &h, sizeof(h)) < sizeof(h)) {
		printf("server: recv_block (ack)\n");
		goto fin2;
	}

	if (ntohl(h.magic) != MAGIC_RCVD) {
		printf("server: invalid ack\n");
		goto fin2;
	}

fin2:
	close(d);
fin1:
	fclose(fp);
fin0:
	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	struct sockaddr_in addr;

	if (argc < 3) {
		printf("%s [ipv4-addr] [port] [(filename to send)]\n",
		       argv[0]);
		goto fin0;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("main: socket\n");
		goto fin0;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	addr.sin_port = htons(atoi(argv[2]));

	if (argc == 3)
		client(fd, &addr);
	else if (argc == 4)
		server(fd, &addr, argv[3]);

	close(fd);
fin0:
	return 0;
}
