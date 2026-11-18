#include "stdout_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void check(int cond, const char* expr)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", expr);
        g_fail++;
    }
}

#define CHECK(cond) check((cond), #cond)

int main(void)
{
    char* a;
    char* b;
    FILE* mem;
    char buf[128];

    stdout_queue_init();
    CHECK(stdout_queue_count() == 0);
    CHECK(stdout_queue_push("{\"evt\":\"report\"}") == 0);
    CHECK(stdout_queue_push("{\"evt\":\"ok\"}") == 0);
    CHECK(stdout_queue_count() == 2);
    a = stdout_queue_pop();
    b = stdout_queue_pop();
    CHECK(a && strcmp(a, "{\"evt\":\"report\"}") == 0);
    CHECK(b && strcmp(b, "{\"evt\":\"ok\"}") == 0);
    CHECK(stdout_queue_pop() == NULL);
    CHECK(stdout_queue_count() == 0);
    free(a);
    free(b);

    CHECK(stdout_queue_push("one") == 0);
    CHECK(stdout_queue_push("two") == 0);
    mem = tmpfile();
    CHECK(mem != NULL);
    stdout_queue_flush(mem);
    CHECK(stdout_queue_count() == 0);
    rewind(mem);
    CHECK(fgets(buf, sizeof(buf), mem) && strcmp(buf, "one\n") == 0);
    CHECK(fgets(buf, sizeof(buf), mem) && strcmp(buf, "two\n") == 0);
    fclose(mem);

    if (g_fail) {
        fprintf(stderr, "%d checks failed\n", g_fail);
        return 1;
    }
    printf("ok stdout_queue\n");
    return 0;
}
