#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE 8192

static int read_nseats(void)
{
	FILE *f = fopen("/sys/module/ticket/parameters/nseats", "r");
	int n = 10;

	if (f) {
		if (fscanf(f, "%d", &n) != 1 || n <= 0)
			n = 10;
		fclose(f);
	}
	return n;
}

static double now_sec(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void do_read(const char *path, int pid)
{
	char buf[BUF_SIZE];
	double start;
	ssize_t n;
	int fd, total = 0, freecnt = 0;
	char *line;

	printf("[%d] ждёт чтение\n", pid);
	fflush(stdout);
	start = now_sec();

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("[%d] ошибка открытия: %s\n", pid, strerror(errno));
		return;
	}
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n < 0) {
		printf("[%d] ошибка чтения: %s\n", pid, strerror(errno));
		return;
	}
	buf[n] = '\0';

	line = strchr(buf, '\n');
	while (line && line[1]) {
		line++;
		total++;
		if (strncmp(line + 5, "free", 4) == 0)
			freecnt++;
		line = strchr(line, '\n');
	}
	printf("[%d] прочитал таблицу: свободно %d из %d (%.1f с)\n",
	       pid, freecnt, total, now_sec() - start);
}

static void do_write(const char *path, int pid, int nseats)
{
	char cmd[64];
	double start;
	int fd, len, seat;

	seat = rand() % nseats;
	printf("[%d] ждёт запись (место %d)\n", pid, seat);
	fflush(stdout);
	start = now_sec();

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		printf("[%d] ошибка открытия: %s\n", pid, strerror(errno));
		return;
	}
	len = snprintf(cmd, sizeof(cmd), "%d user%d", seat, pid);
	if (write(fd, cmd, len) < 0) {
		if (errno == EEXIST)
			printf("[%d] место %d уже занято (%.1f с)\n",
			       pid, seat, now_sec() - start);
		else
			printf("[%d] ошибка записи: %s\n", pid,
			       strerror(errno));
	} else {
		printf("[%d] купил место %d (%.1f с)\n",
		       pid, seat, now_sec() - start);
	}
	close(fd);
}

int main(int argc, char *argv[])
{
	char path[32];
	int service, delay, count, nseats, pid, i, is_read;
	char mode;

	if (argc != 5) {
		printf("Использование: %s <сервис 0|1> <r|w|rand> "
		       "<задержка_мс> <кол-во операций, 0 - бесконечно>\n",
		       argv[0]);
		return 1;
	}
	service = atoi(argv[1]);
	delay = atoi(argv[3]);
	count = atoi(argv[4]);
	if (service < 0 || service > 1 || delay < 0 || count < 0) {
		printf("Неверные аргументы\n");
		return 1;
	}
	if (strcmp(argv[2], "r") == 0)
		mode = 'r';
	else if (strcmp(argv[2], "w") == 0)
		mode = 'w';
	else if (strcmp(argv[2], "rand") == 0)
		mode = 'x';
	else {
		printf("Режим должен быть r, w или rand\n");
		return 1;
	}

	snprintf(path, sizeof(path), "/dev/ticket%d", service);
	nseats = read_nseats();
	pid = getpid();
	srand(time(NULL) ^ pid);

	printf("[%d] старт: сервис %d, режим %s, задержка %d мс\n",
	       pid, service, argv[2], delay);

	for (i = 0; count == 0 || i < count; i++) {
		if (mode == 'x')
			is_read = rand() % 2;
		else
			is_read = (mode == 'r');

		if (is_read)
			do_read(path, pid);
		else
			do_write(path, pid, nseats);
		fflush(stdout);
		usleep(delay * 1000);
	}
	return 0;
}
