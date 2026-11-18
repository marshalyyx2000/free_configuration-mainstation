#include "stdout_queue.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
static CRITICAL_SECTION g_q_lock;
static INIT_ONCE g_q_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK q_lock_init(PINIT_ONCE once, PVOID param, PVOID* ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_q_lock);
    return TRUE;
}
static void q_lock(void)
{
    InitOnceExecuteOnce(&g_q_once, q_lock_init, NULL, NULL);
    EnterCriticalSection(&g_q_lock);
}
static void q_unlock(void)
{
    LeaveCriticalSection(&g_q_lock);
}
#else
static void q_lock(void) {}
static void q_unlock(void) {}
#endif

typedef struct StdoutNode {
    char* line;
    struct StdoutNode* next;
} StdoutNode;

static StdoutNode* g_head;
static StdoutNode* g_tail;
static int g_count;

static char* dup_line(const char* line)
{
    size_t n = strlen(line);
    char* out = (char*)malloc(n + 1);
    if (!out)
        return NULL;
    memcpy(out, line, n + 1);
    return out;
}

void stdout_queue_init(void)
{
    q_lock();
    q_unlock();
}

int stdout_queue_push(const char* line)
{
    StdoutNode* node;
    if (!line)
        return -1;
    node = (StdoutNode*)calloc(1, sizeof(*node));
    if (!node)
        return -1;
    node->line = dup_line(line);
    if (!node->line) {
        free(node);
        return -1;
    }
    q_lock();
    if (g_tail)
        g_tail->next = node;
    else
        g_head = node;
    g_tail = node;
    g_count++;
    q_unlock();
    return 0;
}

char* stdout_queue_pop(void)
{
    StdoutNode* node;
    char* line = NULL;
    q_lock();
    node = g_head;
    if (node) {
        g_head = node->next;
        if (!g_head)
            g_tail = NULL;
        g_count--;
        line = node->line;
    }
    q_unlock();
    free(node);
    return line;
}

int stdout_queue_count(void)
{
    int n;
    q_lock();
    n = g_count;
    q_unlock();
    return n;
}

void stdout_queue_flush(FILE* out)
{
    char* line;
    if (!out)
        return;
    while ((line = stdout_queue_pop()) != NULL) {
        fputs(line, out);
        fputc('\n', out);
        free(line);
    }
    fflush(out);
}
