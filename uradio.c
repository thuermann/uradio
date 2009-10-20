/*
 * $Id: uradio.c,v 1.12 2009/10/20 08:17:58 urs Exp $
 *
 * A simple radio station playing random MP3 files.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void usage(const char *name)
{
	fprintf(stderr, "Usage: %s [-d]\n", name);
}

static void svc(int client, int s, char **names, int count);
static int play(int client, const char *fname, int s);
static int mp3_hdr(const unsigned char *s, int *freq, int *bitrate, int *pad);

static int debug = 0;

int main(int argc, char **argv)
{
	int s, n, count = 0;
	struct sockaddr_in addr;
	pid_t pid;
	char *files[16834];
	char line[1024];
	struct sockaddr_in peer;
	socklen_t peerlen = sizeof(peer);
	int client = 0;
	int errflag = 0;
	int opt;
	int one = 1;

	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			debug = 1;
			break;
		default:
			errflag = 1;
			break;
		}
	}
	if (errflag || optind != argc) {
		usage(argv[0]);
		exit(1);
	}

	while (fgets(line, sizeof(line), stdin)) {
		size_t len = strlen(line);
		if (line[len - 1] == '\n')
			line[len - 1] = 0;
		files[count++] = strdup(line);
	}

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}

	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		exit(1);
	}
	listen(s, 128);

	while (1) {
		if ((n = accept(s, (struct sockaddr *)&peer, &peerlen)) < 0)
			continue;

		printf("New client %.2d from %s\n",
		       ++client, inet_ntoa(peer.sin_addr));

		if ((pid = fork()) < 0) {
			perror("fork");
			close(n);
		} else if (pid > 0) {
			close(n);
			waitpid(pid, NULL, 0);
		} else {
			if (fork() > 0)
				_exit(0);
			close(s);
			svc(client, n, files, count);
			_exit(0);
		}
	}
}

static const char header[] =
	"HTTP/1.0 200 OK\r\n"
	"Content-Type: audio/mpeg\r\n"
	"icy-name: urs' radio\r\n"
	"icy-bitrate: 128\r\ngenre: Mix\r\n"
	"\r\n";

static void svc(int client, int s, char **names, int count)
{
	int idx;

	write(s, header, sizeof(header) - 1);

	srand(time(NULL));
	while (1) {
		idx = rand() % count;
		play(client, names[idx], s);
	}
}

static int play(int client, const char *fname, int s)
{
	static struct timeval next = { 0, 0 };
	static time_t t0 = 0;
	struct timeval now, this;
	FILE *fp;
	int nbytes;
	unsigned char buf[4096];
	int freq, bitrate, pad, fsize, ftime, sl;
	int count;
	char ts[32];

	gettimeofday(&now, NULL);
	strftime(ts, sizeof(ts), "%T", localtime(&now.tv_sec));
	printf("%.2d  %s.%.6ld  play %s\n", client, ts, now.tv_usec, fname);

	if (next.tv_sec == 0) {
		next = now;
		t0   = now.tv_sec;
	}

	if (!(fp = fopen(fname, "r"))) {
		perror(fname);
		return -1;
	}

	fseek(fp, -128, SEEK_END);
	fread(buf, 1, 128, fp);
	write(s, buf, 128);
	rewind(fp);

	count = 0;
	do {
		nbytes = fread(buf, 1, 4, fp);
		if (mp3_hdr(buf, &freq, &bitrate, &pad) < 0)
			break;
		fsize = bitrate * 1000 / 8 * 1152 / freq + pad;
		ftime = 1000000 * 1152 / freq;
		nbytes = fread(buf + 4, 1, fsize - 4, fp);

		gettimeofday(&now, NULL);
		this = next;
		sl = 1000000 * (this.tv_sec - now.tv_sec)
			+ (this.tv_usec - now.tv_usec);
		if ((next.tv_usec += ftime) >= 1000000) {
			next.tv_usec -= 1000000;
			next.tv_sec++;
		}
		if (debug && count < 10) {
			printf("%.2d  this = %ld.%.6ld, now = %ld.%.6ld, "
			       "sl = %6d, next(%+d) = %ld.%.6ld\n",
			       client,
			       this.tv_sec - t0, this.tv_usec,
			       now.tv_sec - t0, now.tv_usec,
			       sl, ftime,
			       next.tv_sec - t0, next.tv_usec);
		}
		if (sl > 0)
			usleep(sl);
		write(s, buf, nbytes + 4);
		if (++count >= 1148)
			break;
	} while (nbytes == fsize - 4);
	if (debug) {
		printf("%.2d  next = %ld.%.6ld\n",
		       client, next.tv_sec - t0, next.tv_usec);
	}
	fclose(fp);

	return 0;
}

static const int tab_bitrate[][2] = {
	{  -1,  -1 },
	{  32,   8 },
	{  40,  16 },
	{  48,  24 },
	{  56,  32 },
	{  64,  40 },
	{  80,  48 },
	{  96,  56 },
	{ 112,  64 },
	{ 128,  80 },
	{ 160,  96 },
	{ 192, 112 },
	{ 224, 128 },
	{ 256, 144 },
	{ 320, 160 },
	{  -1,  -1 },
};

static const int tab_freq[][2] = {
	{ 44100, 22050 },
	{ 48000, 24000 },
	{ 32000, 16000 },
	{    -1,    -1 },
};

static int mp3_hdr(const unsigned char *s, int *freq, int *bitrate, int *pad)
{
	unsigned int h;
	int h_version, h_layer, h_bitrate, h_freq, h_pad, col;

	h = (s[0] << 24) | (s[1] << 16) | (s[2] << 8) | s[3];
	if ((h & 0xfff00000) != 0xfff00000)
		return -1;

	h_version = (h >> 19) & 0x3;
	h_layer   = (h >> 17) & 0x3;
	h_bitrate = (h >> 12) & 0xf;
	h_freq    = (h >> 10) & 0x3;
	h_pad     = (h >>  9) & 0x1;

	if (h_version < 2 || h_layer != 1)
		return -1;

	col = h_version == 3 ? 0 : 1;

	*bitrate = tab_bitrate[h_bitrate][col];
	*freq    = tab_freq[h_freq][col];
	*pad     = h_pad;

	return 0;
}
