#
/*-
 * SPDX-License-Identifier: BSD 2-Clause License
 *
 * Copyright (c) 2026, KusaReMKN
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE	/* for pipe2() */

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/if_tun.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define IFNAME	"lora%d"
#define TUNPATH	"/dev/net/tun"
#define LORAMTU	192
#define BUFLEN	256	/* > LORAMTU + 11 */

#define DEVCHAN	0x07	/* デバイスチャンネル */
#define BRDID	0xFFFF	/* ブロードキャスト識別子 */
#define MYDEVID	0x0000	/* XXX: 自分のデバイス識別子 */

#define MAXHOP	0x03	/* 最大ホップ回数 */

struct loratun {
	int lora;
	int tun;
	int pipe[2];
};

static void init_interface(const char *ifname, int mtu, const char *cidr);
static void init_lora(int fd);
static void usage(void);

static void *receiver(void *arg);
static void *transmitter(void *arg);
static int tun_alloc(char *ifname);

static uint16_t mydevid;

int
main(int argc, char *argv[])
{
	struct loratun lt;
	pthread_t rxthread, txthread;
	int lorafd, tunfd;
	char ifname[IFNAMSIZ] = IFNAME;

	if (argc != 3)
		usage();

	/* LoRa デバイスの準備をする */
	lorafd = open(argv[1], O_RDWR | O_NOCTTY);
	if (lorafd == -1)
		err(EXIT_FAILURE, "%s", argv[1]);
	init_lora(lorafd);

	/* ネットワークインタフェイスを準備する */
	tunfd = tun_alloc(ifname);
	if (tunfd == -1)
		err(EXIT_FAILURE, "tun_alloc");
	init_interface(ifname, LORAMTU, argv[2]);

	/* 自分と通信するためのパイプを準備する */
	if (pipe2(lt.pipe, O_DIRECT) == -1)
		err(EXIT_FAILURE, "pipe2");

	/* 受信用スレッドを起動する */
	lt.lora = lorafd;
	lt.tun = tunfd;
	errno = pthread_create(&rxthread, NULL, receiver, (void *)&lt);
	if (errno != 0)
		err(EXIT_FAILURE, "pthread_create");

	/* 送信用スレッドを起動する */
	lt.lora = lorafd;
	lt.tun = tunfd;
	errno = pthread_create(&txthread, NULL, transmitter, (void *)&lt);
	if (errno != 0)
		err(EXIT_FAILURE, "pthread_create");

	(void)pause();

	return EXIT_FAILURE;
}

/**
 * ネットワークインタフェイスを初期化を代行する。
 * そのインタフェイスの MTU を設定し、IPv4 アドレスを設定し、
 * ネットマスクを設定し、ブロードキャストアドレスを設定し、そして UP する。
 * 途中で失敗した場合は err(3) する。
 *
 * ifname: インタフェイス名
 * mtu: そのインタフェイスの MTU
 * cidr: そのインタフェイスに割り当てる IPv4 アドレスの CIDR 表記
 */
static void
init_interface(const char *ifname, int mtu, const char *cidr)
{
	struct sockaddr_in sin;
	struct ifreq ifr;
	uint32_t mask;
	int masklen, sock;
	char tail;
	char addr[] = "XXX.XXX.XXX.XXX";

	/* 作業用ソケットを開く */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1)
		err(EXIT_FAILURE, "socket");

	/* MTU を設定する */
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	ifr.ifr_mtu = mtu;
	if (ioctl(sock, SIOCSIFMTU, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFMTU");

	/* CIDR 表記を IPv4 アドレスとネットマスクとに分解する */
	if (sscanf(cidr, "%15[^/]/%d%c", addr, &masklen, &tail) != 2
			|| masklen < 0 || masklen > 32)
		errx(EXIT_FAILURE, "%s: %s", cidr, strerror(EINVAL));

	/* アドレスを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	if (inet_aton(addr, &sin.sin_addr) == 0)
		errx(EXIT_FAILURE, "%s: %s", addr, strerror(EINVAL));
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFADDR, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFADDR");

	/* ネットマスクを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	mask = masklen > 0 ? ~0u << (32 - masklen) : 0;
	sin.sin_addr.s_addr = (in_addr_t)htonl(mask);
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFNETMASK, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFNETMASK");

	/* デバイスの送信キューの長さを設定する */
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	ifr.ifr_qlen = 1;
	if (ioctl(sock, SIOCSIFTXQLEN, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFTXQLEN");

	/* ブロードキャストアドレスを設定する */
	(void)memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	if (inet_aton(addr, &sin.sin_addr) == 0)
		errx(EXIT_FAILURE, "%s: %s", addr, strerror(EINVAL));
	mask = masklen > 0 ? ~0u << (32 - masklen) : 0;
	sin.sin_addr.s_addr |= (in_addr_t)htonl(~mask);
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	(void)memcpy(&ifr.ifr_addr, &sin, sizeof(ifr.ifr_addr));
	if (ioctl(sock, SIOCSIFBRDADDR, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFBRDADDR");

	/* インタフェイスを UP する */
	(void)memset(&ifr, 0, sizeof(ifr));
	(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);
	if (ioctl(sock,SIOCGIFFLAGS, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCGIFFLAGS");
	ifr.ifr_flags |= IFF_UP;
	if (ioctl(sock,SIOCSIFFLAGS, &ifr) == -1)
		err(EXIT_FAILURE, "SIOCSIFFLAGS");

	if (close(sock) == -1)
		err(EXIT_FAILURE, "close");
}

/**
 * LoRa 通信モジュールとの通信を初期化する。
 * 実装を簡単にするため、かなりサボっている。
 *
 * fd: LoRa 通信モジュール
 */
static void
init_lora(int fd)
{
	struct termios term;

	if (tcgetattr(fd, &term) == -1)
		err(EXIT_FAILURE, "tcgetattr");
	cfmakeraw(&term);
	if (tcsetattr(fd, TCSANOW, &term) == -1)
		err(EXIT_FAILURE, "tcsetattr");
}

/**
 * パケットを受信する。
 * スレッドとして実行される。
 *
 * arg: (struct loratun *) LoRa デバイスと TUN デバイス
 *
 * 普通は返らない。
 */
static void *
receiver(void *arg)
{
	struct timeval tv;
	static time_t recvat[256];
	ssize_t nbyte, psize, tmp;
	int lorafd, pipefd, tunfd;
	uint16_t destid;
	char rbuf[BUFLEN];
	uint8_t frameid;

	lorafd = ((struct loratun *)arg)->lora;
	pipefd = ((struct loratun *)arg)->pipe[1];	/* 書き込み側 */
	tunfd = ((struct loratun *)arg)->tun;

loop:
	/* パケット長を読み込む（これは長さに含まれない） */
	tmp = read(lorafd, rbuf, 1);
	if (tmp == -1)
		err(EXIT_FAILURE, "read");
	psize = rbuf[0] & 0xFF;

	/* パケットの全体を読み込む */
	nbyte = 0;
	do {
		tmp = read(lorafd, rbuf+nbyte+1, psize-nbyte);
		if (tmp == -1)
			err(EXIT_FAILURE, "read");
		nbyte += tmp;
	} while (nbyte < psize);
	++nbyte;

	/* とりあえず表示しておく */
	(void)gettimeofday(&tv, NULL);
	(void)fprintf(stderr, "\n%zu.%06zu, ",
			(size_t)tv.tv_sec, (size_t)tv.tv_usec);
	(void)fprintf(stderr, "\nfrom LoRa:");
	for (ssize_t i = 0; i < nbyte; i++) {
		if ((i & 0x0F) == 0x00)
			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
		(void)fprintf(stderr, "%02x ", (unsigned)rbuf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)nbyte, (size_t)nbyte);

	/* 通常パケットであれば、そのまま受信する */
	if (rbuf[6] != 'M' && rbuf[7] != 'H') {
		/* とりあえず TUN に全て横流しする（XXX） */
		rbuf[4] = rbuf[5] = 0;
		if (write(tunfd, rbuf+4, nbyte-5) == -1)
			err(EXIT_FAILURE, "write");
		goto loop;
	}

	/* 直近でそのパケットを受信していれば捨てる */
	frameid = rbuf[10];
	if (time(NULL) - recvat[frameid] < 10) {
		warnx("Discarded packet (%02x)", frameid);
		goto loop;
	}

	/* デバイス識別子がブロードキャストか自分宛であれば受信する */
	/* XXX: 現時点では全てブロードキャスト */
	destid = rbuf[8] << 8 | rbuf[9];
	if (destid == BRDID || destid == MYDEVID) {
		/* とりあえず TUN に全て横流しする（XXX） */
		rbuf[7] = rbuf[8] = 0;
		rbuf[9] = rbuf[4];
		rbuf[10] = rbuf[5];	/* EtherType */
		if (write(tunfd, rbuf+7, nbyte-8) == -1)
			err(EXIT_FAILURE, "write");
		/* 壊した部分を書き戻す */
		rbuf[7] = 'H';
		rbuf[8] = destid >> 8 & 0xFF;
		rbuf[9] = destid      & 0xFF;
		rbuf[10] = frameid;
	}

	/* 真に自分宛でなければ TTL を減らして再送する */
	if (destid != MYDEVID && --rbuf[3] > 0) {
		/* 送信用ヘッダを用意する */
		rbuf[0] = BRDID >> 8 & 0xFF;
		rbuf[1] = BRDID      & 0xFF;	/* 宛先はブロードキャスト */
		rbuf[2] = DEVCHAN;		/* 宛先は 7 ch */
		/* チェックサムを再計算する */
		rbuf[nbyte-1] = 0;
		for (size_t i = 0; i < nbyte-1; i++)
			rbuf[nbyte-1] ^= rbuf[i];
		if (write(pipefd, rbuf, nbyte) == -1)
			err(EXIT_FAILURE, "write");
	}

	/* パケット受信時刻を記録する */
	recvat[frameid] = time(NULL);

	goto loop;
	/* NOTREACHED */

	return NULL;
}

/**
 * パケットを送信する。
 * スレッドとして実行される。
 *
 * arg: (struct loratun *) LoRa デバイスと TUN デバイス
 *
 * 普通は返らない。
 */
static void *
transmitter(void *arg)
{
	struct timeval tv;
	fd_set rfds;
	ssize_t nbyte;
	int lorafd, nfds, pipefd, tunfd;
	char rbuf[BUFLEN], tbuf[BUFLEN];
	char chksum;

	lorafd = ((struct loratun *)arg)->lora;
	pipefd = ((struct loratun *)arg)->pipe[0];	/* 読み出し側 */
	tunfd = ((struct loratun *)arg)->tun;

loop:
	/* TUN と PIPE とを同時に監視する */
	FD_ZERO(&rfds);
	FD_SET(pipefd, &rfds);
	FD_SET(tunfd, &rfds);
	nfds = MAX(pipefd, tunfd) + 1;
	if (select(nfds, &rfds, NULL, NULL, NULL) == -1)
		err(EXIT_FAILURE, "select");

	/* PIPE からの読み込みなら全てを LoRa に投げる */
	if (FD_ISSET(pipefd, &rfds)) {
		nbyte = read(pipefd, tbuf, sizeof(tbuf));
		if (nbyte == -1)
			err(EXIT_FAILURE, "read");
		goto submit;
	}
	/* 以降 TUN のみ */

	/* パケットを読み出す */
	nbyte = read(tunfd, rbuf, sizeof(rbuf));
	if (nbyte == -1)
		err(EXIT_FAILURE, "read");

	/* とりあえず表示しておく */
//	(void)fprintf(stderr, "\nfrom TUN:");
//	for (ssize_t i = 0; i < nbyte; i++) {
//		if ((i & 0x0F) == 0x00)
//			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
//		(void)fprintf(stderr, "%02x ", (unsigned)rbuf[i] & 0xFF);
//	}
//	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)nbyte, (size_t)nbyte);

	/* 本来であればここで ARP する（XXX） */
	/* ARP の結果 IPv4 以外は通れなくなるんだけど、よくわからない */

	/* とりあえす LoRa に全て横流しする（XXX） */
	tbuf[0] = BRDID >> 8 & 0xFF;
	tbuf[1] = BRDID      & 0xFF;	/* 宛先はブロードキャスト */
	tbuf[2] = DEVCHAN;		/* 宛先は 7 ch */
	tbuf[3] = MAXHOP;		/* 最大ホップ数は 3 */
	tbuf[4] = rbuf[2];
	tbuf[5] = rbuf[3];		/* L3 プロトコル */
	tbuf[6] = 'M';
	tbuf[7] = 'H';			/* マルチホップパケット */
	tbuf[8] = BRDID >> 8 & 0xFF;
	tbuf[9] = BRDID      & 0xFF;	/* 宛先デバイスはブロードキャスト */
	tbuf[10] = 0x00;		/* パケット ID はとりあえず 0 */
	for (ssize_t i = 0; i < nbyte-4; i++)
		tbuf[11+i] = rbuf[4+i];
	nbyte += 8;		/* - sizeof(pi) + 9 */
	/* チェックサム計算 */
	chksum = 0;
	for (ssize_t i = 0; i < nbyte-1; i++)
		chksum ^= tbuf[i];
	tbuf[10] = chksum;
	tbuf[nbyte-1] = 0;

submit:	/* とりあえず表示しておく */
	(void)gettimeofday(&tv, NULL);
	(void)fprintf(stderr, "\n%zu.%06zu, ",
			(size_t)tv.tv_sec, (size_t)tv.tv_usec);
	(void)fprintf(stderr, "\nto LoRa:");
	for (ssize_t i = 0; i < nbyte; i++) {
		if ((i & 0x0F) == 0x00)
			(void)fprintf(stderr, "\n%#04zx:\t", (size_t)i);
		(void)fprintf(stderr, "%02x ", (unsigned)tbuf[i] & 0xFF);
	}
	(void)fprintf(stderr, "\n%04zx (%zd)\n", (size_t)nbyte, (size_t)nbyte);

	/* TUN も PIPE もここで送信される */
	if (write(lorafd, tbuf, (size_t)nbyte) == -1)
		err(EXIT_FAILURE, "write");

	/* パケット境界を示すために少し待つ */
	(void)usleep(70000);

	goto loop;
	/* NOTREACHED */

	return NULL;
}

/**
 * TUN インタフェイスを作成する。
 *
 * ifname: インタフェイス名のテンプレート
 *
 * 成功するとファイルデスクリプタを返し、
 * ifname の指す先を作られたインタフェイス名で更新する。
 * 失敗すると -1 を返し、それらしい errno を設定する。
 */
static int
tun_alloc(char *ifname)
{
	struct ifreq ifr;
	int error, fd;

	fd = open(TUNPATH, O_RDWR);
	if (fd == -1)
		return -1;

	(void)memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN;
	if (ifname != NULL && *ifname != '\0')
		(void)snprintf(ifr.ifr_name, IFNAMSIZ, "%s", ifname);

	if (ioctl(fd, TUNSETIFF, (void *)&ifr) == -1) {
		error = errno;
		(void)close(fd);
		errno = error;
		return -1;
	}
	(void)snprintf(ifname, IFNAMSIZ, "%s", ifr.ifr_name);

	return fd;
}

/**
 * 使い方を出力して自滅する。
 */
static void
usage(void)
{
	fprintf(stderr, "usage: shinjicom <LoRa> <CIDR>\n");
	exit(EXIT_FAILURE);
}
