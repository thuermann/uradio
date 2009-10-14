/*
 * $Id: uradio.c,v 1.1 2009/10/13 21:38:41 urs Exp $
 *
 * A simple radio station playing random MP3 files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

char *files[16834];

int main(int argc, char **argv)
{
	int s, n, count = 0;
	struct sockaddr_in addr;
	pid_t pid;
	char line[1024];

	while (gets(line))
		files[count++] = strdup(line);

	if ((s = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(1);
	}
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(8080);
	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		exit(1);
	}
	listen(s, 128);

	while (1) {
		if ((n = accept(s, NULL, NULL)) < 0)
			continue;
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
			svc(n, files, count);
			_exit(0);
		}
	}
}

char header[] =
	"HTTP/1.0 200 OK\r\n"
	"Content-Type: audio/mpeg\r\n"
	"icy-name: urs' radio\r\n"
	"icy-bitrate: 128\r\ngenre: Mix\r\n"
	"\r\n";

int svc(int s, char **names, int count)
{
	int idx;

	write(s, header, sizeof(header) - 1);

	while (1) {
		idx = rand() % count;
		play(names[idx], s);
	}
}

int play(char *fname, int s)
{
	FILE *fp;
	int nbytes;
	unsigned char buf[4096];
	int freq, bitrate, pad, size, time, sl;
	struct timeval now, next;
	int count;

	printf("play %s\n", fname);

	if (!(fp = fopen(fname, "r"))) {
		perror(fname);
		return -1;
	}
	fseek(fp, -128, SEEK_END);
	fread(buf, 1, 128, fp);
	write(s, buf, 128);
	rewind(fp);
	memset(&next, 0, sizeof(next));
	count = 0;
	do {
		nbytes = fread(buf, 1, 4, fp);
		if (mp3_hdr(buf, &freq, &bitrate, &pad) < 0)
			break;
		size = bitrate * 1000 / 8 * 1152 / freq + pad;
		time = 1000000 * 1152 / freq;
		nbytes = fread(buf + 4, 1, size - 4, fp);
#ifdef DEBUG
		printf("time %d\n", time);
#endif
		gettimeofday(&now, NULL);
		if (next.tv_sec) {
			sl = 1000000 * (next.tv_sec - now.tv_sec)
				+ (next.tv_usec - now.tv_usec);
#ifdef DEBUG
			printf("%ld.%.6ld %ld.%.6ld %ld\n",
			       now.tv_sec, now.tv_usec,
			       next.tv_sec, next.tv_usec,
			       sl);
#endif
			if (sl > 0)
				usleep(sl);
		} else {
			next = now;
			sl = 0;
#ifdef DEBUG
			printf("%ld.%.6ld %ld.%.6ld %ld\n",
			       now.tv_sec, now.tv_usec,
			       next.tv_sec, next.tv_usec,
			       sl);
#endif
		}
		if ((next.tv_usec += time) >= 1000000) {
			next.tv_usec -= 1000000;
			next.tv_sec++;
		}
#ifdef DEBUG
		printf("%ld.%.6ld\n",
		       next.tv_sec, next.tv_usec);
#endif
		write(s, buf, nbytes + 4);
		if (count++ >= 1000)
			break;
	} while (nbytes == size - 4);
	fclose(fp);

	return 0;
}

static int tab_bitrate[][2] = {
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

static int tab_freq[][2] = {
	{ 44100, 22050 },
	{ 48000, 24000 },
	{ 32000, 16000 },
	{    -1,    -1 },
};

int mp3_hdr(unsigned char *s, int *freq, int *bitrate, int *pad)
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