/*
 * Copyright 2026, Andrea Bernardi
 * SPDX-License-Identifier: MIT
 *
 * Checks poll() and select() timeouts, alone and on a TCP socket that gets
 * an HTTP reply: prints how long each call took and what it returned.
 * Usage: polltest [host-ip] (default 104.20.23.154, example.com)
 */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static double
now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

int
main(int argc, char** argv)
{
	int fds[2];
	pipe(fds);
	struct pollfd p = { fds[0], POLLIN, 0 };
	double t = now();
	int r = poll(&p, 1, 1500);
	printf("poll pipe 1500 ms: r=%d in %.3f s\n", r, now() - t);

	fd_set set;
	FD_ZERO(&set);
	FD_SET(fds[0], &set);
	struct timeval tv = { 1, 500000 };
	t = now();
	r = select(fds[0] + 1, &set, NULL, NULL, &tv);
	printf("select pipe 1.5 s: r=%d in %.3f s\n", r, now() - t);
	fflush(stdout);

	int s = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(80);
	inet_pton(AF_INET, argc > 1 ? argv[1] : "104.20.23.154", &a.sin_addr);
	if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
		printf("connect: %s\n", strerror(errno));
		return 1;
	}
	const char* req = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
	send(s, req, strlen(req), 0);
	size_t total = 0;
	double start = now();
	while (now() - start < 20) {
		struct pollfd sp = { s, POLLIN, 0 };
		t = now();
		r = poll(&sp, 1, 1000);
		int avail = -1;
		char buf[4096];
		ssize_t n = 0;
		if (r > 0)
			n = recv(s, buf, sizeof(buf), MSG_DONTWAIT);
		printf("poll sock: r=%d revents=0x%x in %.3f s, recv %zd (%s)\n", r,
			sp.revents, now() - t, n, n < 0 ? strerror(errno) : "ok");
		fflush(stdout);
		(void)avail;
		if (r > 0 && n == 0)
			break;
		if (n > 0)
			total += n;
	}
	printf("total %zu bytes\n", total);

	// keep-alive connection closed like curl does at exit: shutdown(SHUT_WR),
	// then wait for the peer to close
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
		printf("connect 2: %s\n", strerror(errno));
		return 1;
	}
	req = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
	send(s, req, strlen(req), 0);
	char buf[8192];
	usleep(500000);
	ssize_t n = recv(s, buf, sizeof(buf), MSG_DONTWAIT);
	printf("keep-alive reply %zd\n", n);
	t = now();
	r = shutdown(s, SHUT_WR);
	printf("shutdown: r=%d in %.3f s\n", r, now() - t);
	fflush(stdout);
	for (int i = 0; i < 5; i++) {
		struct pollfd sp = { s, POLLIN, 0 };
		t = now();
		r = poll(&sp, 1, 1000);
		n = r > 0 ? recv(s, buf, sizeof(buf), MSG_DONTWAIT) : 0;
		printf("after shutdown poll: r=%d revents=0x%x in %.3f s, recv %zd\n",
			r, sp.revents, now() - t, n);
		fflush(stdout);
	}
	t = now();
	r = close(s);
	printf("close: r=%d in %.3f s\n", r, now() - t);
	fflush(stdout);

	// O_NONBLOCK set with fcntl(), then recv() without MSG_DONTWAIT, before
	// and after the reply has been read (curl does this at exit)
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(s, (struct sockaddr*)&a, sizeof(a)) != 0) {
		printf("connect 3: %s\n", strerror(errno));
		return 1;
	}
	fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
	printf("F_GETFL 0x%x\n", fcntl(s, F_GETFL));
	t = now();
	n = recv(s, buf, sizeof(buf), 0);
	printf("nonblock recv, nothing sent: %zd (%s) in %.3f s\n", n,
		n < 0 ? strerror(errno) : "ok", now() - t);
	fflush(stdout);
	send(s, req, strlen(req), 0);
	usleep(500000);
	t = now();
	n = recv(s, buf, sizeof(buf), 0);
	printf("nonblock recv, reply: %zd in %.3f s\n", n, now() - t);
	fflush(stdout);
	t = now();
	n = recv(s, buf, sizeof(buf), 0);
	printf("nonblock recv, drained: %zd (%s) in %.3f s\n", n,
		n < 0 ? strerror(errno) : "ok", now() - t);
	fflush(stdout);
	return 0;
}
