CC :=$(ATH_CROSS_COMPILE_TYPE)gcc

all:
	$(CC) -Wall -DUSER_KEYS -g -I../../include -I../../../include -I../../wlan/include -I../../os/linux/include wmiconfig.c -o wmiconfig
