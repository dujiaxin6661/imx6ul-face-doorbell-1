#include "gpio_control.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#if MOCK_MODE

int gpio_init(void) {
    printf("[GPIO] MOCK: simulated\n");
    return 0;
}
void gpio_release(void) {}
int open_door(void) {
    printf("[GPIO] MOCK open_door (2 sec)\n");
    sleep(2);
    return 0;
}

#else

/*
  sysfs GPIO 控制 — 零外部依赖，纯文件 I/O
  操作 /sys/class/gpio/export, gpioN/value, gpioN/direction
*/

static char gpio_path[64];
static int  gpio_exported = 0;

static int gpio_write(const char *file, const char *value)
{
    int fd = open(file, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, value, strlen(value));
    close(fd);
    return 0;
}

int gpio_init(void)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", GPIO_LINE);

    /* 导出 GPIO */
    if (gpio_write("/sys/class/gpio/export", buf) < 0) {
        perror("[GPIO] export");
        return -1;
    }
    gpio_exported = 1;
    usleep(100000);  /* 等待 sysfs 创建 gpioN 目录 */

    snprintf(gpio_path, sizeof(gpio_path),
             "/sys/class/gpio/gpio%d", GPIO_LINE);

    /* 设为输出 */
    char dir_path[80];
    snprintf(dir_path, sizeof(dir_path), "%s/direction", gpio_path);
    if (gpio_write(dir_path, "out") < 0) {
        perror("[GPIO] direction");
        return -1;
    }

    /* 初始低电平 */
    char val_path[80];
    snprintf(val_path, sizeof(val_path), "%s/value", gpio_path);
    gpio_write(val_path, "0");

    printf("[GPIO] sysfs gpio%d ready\n", GPIO_LINE);
    return 0;
}

void gpio_release(void)
{
    if (gpio_exported) {
        char val_path[80];
        snprintf(val_path, sizeof(val_path), "%s/value", gpio_path);
        gpio_write(val_path, "0");

        char buf[32];
        snprintf(buf, sizeof(buf), "%d", GPIO_LINE);
        gpio_write("/sys/class/gpio/unexport", buf);
        gpio_exported = 0;
    }
}

int open_door(void)
{
    if (!gpio_exported) return -1;

    char val_path[80];
    snprintf(val_path, sizeof(val_path), "%s/value", gpio_path);

    printf("[GPIO] open_door: HIGH (%d sec)\n", GPIO_OPEN_DURATION);
    gpio_write(val_path, "1");
    sleep(GPIO_OPEN_DURATION);
    gpio_write(val_path, "0");
    printf("[GPIO] open_door: LOW (locked)\n");
    return 0;
}

#endif
