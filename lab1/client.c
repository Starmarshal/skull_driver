#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#define BUF_SIZE 8192

static void show_seats(const char *path)
{
	char buf[BUF_SIZE];
	ssize_t n;
	int fd;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		printf("Не удалось открыть %s: %s\n", path, strerror(errno));
		return;
	}
	printf("Чтение таблицы (ждём, если идёт продажа)...\n");
	while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
		buf[n] = '\0';
		fputs(buf, stdout);
	}
	if (n < 0)
		printf("Ошибка чтения: %s\n", strerror(errno));
	close(fd);
}

static void buy_seat(const char *path, int seat, const char *name)
{
	char cmd[64];
	int fd, len;

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		printf("Не удалось открыть %s: %s\n", path, strerror(errno));
		return;
	}
	len = snprintf(cmd, sizeof(cmd), "%d %s", seat, name);
	printf("Покупка (ждём, пока все читатели и писатели закончат)...\n");
	if (write(fd, cmd, len) < 0) {
		if (errno == EEXIST)
			printf("Место %d уже продано\n", seat);
		else if (errno == EINVAL)
			printf("Неверный номер места или имя\n");
		else
			printf("Ошибка записи: %s\n", strerror(errno));
	} else {
		printf("Место %d куплено на имя %s\n", seat, name);
	}
	close(fd);
}

int main(void)
{
	char path[32];
	char name[32];
	int service, choice, seat;

	printf("Выберите сервис продажи билетов (0 или 1): ");
	if (scanf("%d", &service) != 1 || service < 0 || service > 1) {
		printf("Неверный номер сервиса\n");
		return 1;
	}
	snprintf(path, sizeof(path), "/dev/ticket%d", service);

	for (;;) {
		printf("\nСервис %d\n", service);
		printf("1 - показать все места\n");
		printf("2 - купить место\n");
		printf("0 - выход\n> ");
		if (scanf("%d", &choice) != 1)
			break;

		if (choice == 0) {
			break;
		} else if (choice == 1) {
			show_seats(path);
		} else if (choice == 2) {
			printf("Номер места: ");
			if (scanf("%d", &seat) != 1)
				break;
			printf("Имя покупателя: ");
			if (scanf("%31s", name) != 1)
				break;
			buy_seat(path, seat, name);
		} else {
			printf("Неизвестная команда\n");
		}
	}
	return 0;
}
