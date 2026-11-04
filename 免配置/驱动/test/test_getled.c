#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kemyond.h"

static void usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s <led_id>\n", prog);
    printf("  %s all\n", prog);
    printf("\n");
    printf("led_id:\n");
    printf("  %d: LED_RUN\n", LED_RUN);
    printf("  %d: LED_ALARM\n", LED_ALARM);
    printf("  %d: LED_BACKUP\n", LED_BACKUP);
}

static int print_one_led(int id, const char *name)
{
    int ret;

    ret = GetLEDValue(id);
    if (ret < 0) {
        printf("%s get failed\n", name);
        return -1;
    }

    printf("%s = %d, %s\n", name, ret, ret ? "ON" : "OFF");
    return 0;
}

int main(int argc, char *argv[])
{
    int id;
    int ret;

    if (argc != 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "all") == 0) {
        ret = 0;

        if (print_one_led(LED_RUN, "LED_RUN") < 0)
            ret = 1;
        if (print_one_led(LED_ALARM, "LED_ALARM") < 0)
            ret = 1;
        if (print_one_led(LED_BACKUP, "LED_BACKUP") < 0)
            ret = 1;

        return ret;
    }

    id = atoi(argv[1]);

    switch (id) {
    case LED_RUN:
        return print_one_led(id, "LED_RUN") < 0 ? 1 : 0;
    case LED_ALARM:
        return print_one_led(id, "LED_ALARM") < 0 ? 1 : 0;
    case LED_BACKUP:
        return print_one_led(id, "LED_BACKUP") < 0 ? 1 : 0;
    default:
        usage(argv[0]);
        return 1;
    }
}
