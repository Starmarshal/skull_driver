#!/bin/sh

if [ ! -e /dev/ticket0 ] || [ ! -e /dev/ticket1 ]; then
	echo "Устройства не найдены, загрузите модуль: sudo insmod ticket.ko"
	exit 1
fi
if [ ! -x ./autoclient ]; then
	echo "Нет ./autoclient, выполните make"
	exit 1
fi

pids=""
trap 'echo; echo "Остановка клиентов..."; kill $pids 2>/dev/null; exit 0' INT TERM

start() {
	./autoclient "$@" &
	pids="$pids $!"
}

echo "Запуск 10 клиентов (Ctrl+C для остановки)"
echo "Журнал драйвера: sudo dmesg -w | grep ticket"

start 0 r 300 0
start 0 r 700 0
start 0 w 500 0
start 0 rand 400 0
start 0 rand 1000 0
start 1 r 500 0
start 1 w 300 0
start 1 w 800 0
start 1 rand 600 0
start 1 rand 200 0

wait
