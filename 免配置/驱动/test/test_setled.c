#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "kemyond.h"

static void usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s <led_id> <0|1>\n", prog);
    printf("\n");
    printf("led_id:\n");
    printf("  %d: LED_RUN\n", LED_RUN);
    printf("  %d: LED_ALARM\n", LED_ALARM);
    printf("  %d: LED_BACKUP\n", LED_BACKUP);
    printf("\n");
    printf("value:\n");
    printf("  1: LED ON\n");
    printf("  0: LED OFF\n");
}

int main(int argc, char *argv[])
{
    int id;
    int value;
    int ret;

    if (argc != 3) {
        usage(argv[0]);
        return 1;
    }

    id = atoi(argv[1]);
    value = atoi(argv[2]);

    if (value != 0 && value != 1) {
        usage(argv[0]);
        return 1;
    }

    ret = SetLEDValue(id, value ? true : false);
    if (ret < 0) {
        printf("SetLEDValue failed, id=%d value=%d\n", id, value);
        return 1;
    }

    printf("SetLEDValue ok, id=%d value=%d\n", id, value);
    return 0;
}
