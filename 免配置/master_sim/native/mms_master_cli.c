/*
 * mms_master_cli - JSONL stdin/stdout MMS master for master_sim GUI.
 */
#include "iec61850_client.h"
#include "hal_thread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

const char* ied_error_message(IedClientError error);

#define MAX_LINE 8192
#define MAX_RCB 24
#define MAX_RCB_REF 280

static IedConnection g_con = NULL;
static volatile int g_tickRun = 0;
static Thread g_tickThread = NULL;

typedef struct {
    bool used;
    char rcbGetRef[MAX_RCB_REF];
    char handlerRef[MAX_RCB_REF];
    char dsRef[MAX_RCB_REF];
    ClientReportControlBlock rcb;
    ClientDataSet clientDataSet;
    LinkedList dataSetDirectory;
} ActiveRcb;

static ActiveRcb g_rcbs[MAX_RCB];

static void json_escape_print(FILE* out, const char* s) {
    fputc('"', out);
    if (s) {
        for (const char* p = s; *p; p++) {
            if (*p == '\\' || *p == '"') {
                fputc('\\', out);
            }
            fputc(*p, out);
        }
    }
    fputc('"', out);
}

static void emit_error(const char* msg) {
    printf("{\"evt\":\"error\",\"msg\":");
    json_escape_print(stdout, msg ? msg : "unknown");
    printf("}\n");
    fflush(stdout);
}

static void emit_ok(const char* cmd) {
    printf("{\"evt\":\"ok\",\"cmd\":");
    json_escape_print(stdout, cmd);
    printf("}\n");
    fflush(stdout);
}

static const char* json_str(const char* line, const char* key, char* buf, size_t bufsz) {
    buf[0] = 0;
    if (!line || !key) {
        return buf;
    }
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(line, pattern);
    if (!p) {
        return buf;
    }
    p = strchr(p, ':');
    if (!p) {
        return buf;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return buf;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < bufsz) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        buf[i++] = *p++;
    }
    buf[i] = 0;
    return buf;
}

static int json_int(const char* line, const char* key, int def) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(line, pattern);
    if (!p) {
        return def;
    }
    p = strchr(p, ':');
    if (!p) {
        return def;
    }
    return atoi(p + 1);
}

static bool comm_stval_from_item(MmsValue* item, bool* out) {
    if (!item || !out) {
        return false;
    }
    if (MmsValue_getType(item) == MMS_BOOLEAN) {
        *out = MmsValue_getBoolean(item);
        return true;
    }
    if (MmsValue_getType(item) == MMS_STRUCTURE && MmsValue_getArraySize(item) > 0) {
        MmsValue* st = MmsValue_getElement(item, 0);
        if (st && MmsValue_getType(st) == MMS_BOOLEAN) {
            *out = MmsValue_getBoolean(st);
            return true;
        }
    }
    return false;
}

static void print_mms_json(MmsValue* val);

static void print_mms_json(MmsValue* val) {
    if (!val) {
        printf("null");
        return;
    }
    char buf[512];
    switch (MmsValue_getType(val)) {
    case MMS_BOOLEAN:
        printf("%s", MmsValue_getBoolean(val) ? "true" : "false");
        break;
    case MMS_INTEGER:
        printf("%lld", (long long)MmsValue_toInt64(val));
        break;
    case MMS_UNSIGNED:
        printf("%llu", (unsigned long long)MmsValue_toUint32(val));
        break;
    case MMS_FLOAT:
        printf("%g", MmsValue_toFloat(val));
        break;
    case MMS_VISIBLE_STRING:
    case MMS_STRING:
        buf[0] = 0;
        MmsValue_printToBuffer(val, buf, (int)sizeof(buf));
        json_escape_print(stdout, buf);
        break;
    case MMS_STRUCTURE:
    case MMS_ARRAY: {
        int n = MmsValue_getArraySize(val);
        printf("[");
        for (int i = 0; i < n; i++) {
            if (i) {
                printf(",");
            }
            print_mms_json(MmsValue_getElement(val, i));
        }
        printf("]");
        break;
    }
    default:
        buf[0] = 0;
        MmsValue_printToBuffer(val, buf, (int)sizeof(buf));
        json_escape_print(stdout, buf);
        break;
    }
}

static void tick_loop(void* param) {
    (void)param;
    while (g_tickRun) {
        if (g_con && IedConnection_getState(g_con) == IED_STATE_CONNECTED) {
            IedConnection_tick(g_con);
        }
        Thread_sleep(1);
    }
}

static void start_tick(void) {
    if (g_tickRun) {
        return;
    }
    g_tickRun = 1;
    g_tickThread = Thread_create((ThreadExecutionFunction)tick_loop, NULL, false);
}

static void stop_tick(void) {
    g_tickRun = 0;
    if (g_tickThread) {
        Thread_destroy(g_tickThread);
        g_tickThread = NULL;
    }
}

static ActiveRcb* find_rcb_by_handler(const char* handlerRef) {
    for (int i = 0; i < MAX_RCB; i++) {
        if (g_rcbs[i].used && strcmp(g_rcbs[i].handlerRef, handlerRef) == 0) {
            return &g_rcbs[i];
        }
    }
    return NULL;
}

static void reportCallback(void* parameter, ClientReport report) {
    ActiveRcb* slot = (ActiveRcb*)parameter;
    if (!slot || !report) {
        return;
    }

    MmsValue* dataSetValues = ClientReport_getDataSetValues(report);
    const char* rcbRef = ClientReport_getRcbReference(report);

    printf("{\"evt\":\"report\",\"rcb\":");
    json_escape_print(stdout, rcbRef ? rcbRef : "");
    printf(",\"items\":[");
    int first = 1;
    int n = dataSetValues ? MmsValue_getArraySize(dataSetValues) : 0;
    for (int i = 0; i < n; i++) {
        ReasonForInclusion reason = ClientReport_getReasonForInclusion(report, i);
        if (reason == IEC61850_REASON_NOT_INCLUDED) {
            continue;
        }
        if (!first) {
            printf(",");
        }
        first = 0;
        const char* name = "";
        if (slot->dataSetDirectory) {
            LinkedList entry = LinkedList_get(slot->dataSetDirectory, i);
            if (entry && entry->data) {
                name = (const char*)entry->data;
            }
        }
        printf("{\"idx\":%d,\"reason\":%d,\"name\":", i, (int)reason);
        json_escape_print(stdout, name);
        printf(",\"value\":");
        print_mms_json(MmsValue_getElement(dataSetValues, i));
        if (ClientReport_hasTimestamp(report)) {
            printf(",\"ts_ms\":%llu", (unsigned long long)ClientReport_getTimestamp(report));
        }
        printf("}");
    }
    printf("]}\n");
    fflush(stdout);
}

static void free_rcb_slot(ActiveRcb* slot) {
    if (!slot || !slot->used) {
        return;
    }
    if (g_con) {
        IedConnection_uninstallReportHandler(g_con, slot->handlerRef);
    }
    if (slot->rcb) {
        ClientReportControlBlock_destroy(slot->rcb);
        slot->rcb = NULL;
    }
    if (slot->clientDataSet) {
        ClientDataSet_destroy(slot->clientDataSet);
        slot->clientDataSet = NULL;
    }
    if (slot->dataSetDirectory) {
        LinkedList_destroy(slot->dataSetDirectory);
        slot->dataSetDirectory = NULL;
    }
    slot->used = false;
}

static ActiveRcb* alloc_rcb_slot(void) {
    for (int i = 0; i < MAX_RCB; i++) {
        if (!g_rcbs[i].used) {
            memset(&g_rcbs[i], 0, sizeof(g_rcbs[i]));
            g_rcbs[i].used = true;
            return &g_rcbs[i];
        }
    }
    return NULL;
}

static void cmd_connect(const char* line) {
    char host[256];
    json_str(line, "host", host, sizeof(host));
    int port = json_int(line, "port", 102);
    if (host[0] == 0) {
        emit_error("missing host");
        return;
    }
    if (g_con) {
        stop_tick();
        IedConnection_close(g_con);
        IedConnection_destroy(g_con);
        g_con = NULL;
    }
    for (int i = 0; i < MAX_RCB; i++) {
        free_rcb_slot(&g_rcbs[i]);
    }

    g_con = IedConnection_create();
    IedConnection_setRequestTimeout(g_con, 30000);
    IedClientError err = IED_ERROR_OK;
    IedConnection_connect(g_con, &err, host, port);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        IedConnection_destroy(g_con);
        g_con = NULL;
        return;
    }
    start_tick();
    printf("{\"evt\":\"connected\",\"host\":");
    json_escape_print(stdout, host);
    printf(",\"port\":%d}\n", port);
    fflush(stdout);
}

static void cmd_disconnect(void) {
    stop_tick();
    for (int i = 0; i < MAX_RCB; i++) {
        free_rcb_slot(&g_rcbs[i]);
    }
    if (g_con) {
        IedConnection_close(g_con);
        IedConnection_destroy(g_con);
        g_con = NULL;
    }
    printf("{\"evt\":\"disconnected\"}\n");
    fflush(stdout);
}

static void cmd_read_comm_state(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char dsRef[512];
    json_str(line, "ref", dsRef, sizeof(dsRef));
    if (dsRef[0] == 0) {
        strcpy(dsRef, "SubStationLD0/LLN0$dsCommState");
    }
    IedClientError err = IED_ERROR_OK;
    ClientDataSet ds = IedConnection_readDataSetValues(g_con, &err, dsRef, NULL);
    if (err != IED_ERROR_OK || !ds) {
        emit_error(ied_error_message(err));
        return;
    }
    MmsValue* values = ClientDataSet_getValues(ds);
    int n = values ? MmsValue_getArraySize(values) : 0;
    printf("{\"evt\":\"comm_state\",\"ref\":");
    json_escape_print(stdout, dsRef);
    printf(",\"entries\":[");
    for (int i = 0; i < n; i++) {
        bool st = false;
        bool ok = comm_stval_from_item(MmsValue_getElement(values, i), &st);
        if (i) {
            printf(",");
        }
        printf("{\"idx\":%d,\"stVal\":%s,\"valid\":%s}", i + 1, st ? "true" : "false", ok ? "true" : "false");
    }
    printf("]}\n");
    fflush(stdout);
    ClientDataSet_destroy(ds);
}

static void cmd_get_ld_list(void) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getLogicalDeviceList(g_con, &err);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        return;
    }
    printf("{\"evt\":\"ld_list\",\"devices\":[");
    LinkedList el = list;
    int first = 1;
    while (el) {
        if (!first) {
            printf(",");
        }
        first = 0;
        json_escape_print(stdout, (const char*)el->data);
        el = el->next;
    }
    printf("]}\n");
    fflush(stdout);
    LinkedList_destroy(list);
}

static void cmd_get_datasets(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char ld[256];
    json_str(line, "ld", ld, sizeof(ld));
    if (ld[0] == 0) {
        emit_error("missing ld");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getLogicalDeviceDataSets(g_con, &err, ld);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        return;
    }
    printf("{\"evt\":\"datasets\",\"ld\":");
    json_escape_print(stdout, ld);
    printf(",\"names\":[");
    LinkedList el = list;
    int first = 1;
    while (el) {
        if (!first) {
            printf(",");
        }
        first = 0;
        json_escape_print(stdout, (const char*)el->data);
        el = el->next;
    }
    printf("]}\n");
    fflush(stdout);
    LinkedList_destroy(list);
}

static void cmd_get_dataset_directory(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char ref[512];
    json_str(line, "ref", ref, sizeof(ref));
    if (ref[0] == 0) {
        emit_error("missing ref");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getDataSetDirectory(g_con, &err, ref, NULL);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        return;
    }
    printf("{\"evt\":\"dataset_directory\",\"ref\":");
    json_escape_print(stdout, ref);
    printf(",\"members\":[");
    LinkedList el = list;
    int first = 1;
    while (el) {
        if (!first) {
            printf(",");
        }
        first = 0;
        json_escape_print(stdout, (const char*)el->data);
        el = el->next;
    }
    printf("]}\n");
    fflush(stdout);
    LinkedList_destroy(list);
}

static void cmd_read_dataset(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char ref[512];
    json_str(line, "ref", ref, sizeof(ref));
    if (ref[0] == 0) {
        emit_error("missing ref");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList dir = IedConnection_getDataSetDirectory(g_con, &err, ref, NULL);
    ClientDataSet ds = IedConnection_readDataSetValues(g_con, &err, ref, NULL);
    if (err != IED_ERROR_OK || !ds) {
        if (dir) {
            LinkedList_destroy(dir);
        }
        emit_error(ied_error_message(err));
        return;
    }
    MmsValue* values = ClientDataSet_getValues(ds);
    int n = values ? MmsValue_getArraySize(values) : 0;
    printf("{\"evt\":\"dataset\",\"ref\":");
    json_escape_print(stdout, ref);
    printf(",\"rows\":[");
    for (int i = 0; i < n; i++) {
        const char* name = "";
        if (dir) {
            LinkedList entry = LinkedList_get(dir, i);
            if (entry && entry->data) {
                name = (const char*)entry->data;
            }
        }
        if (i) {
            printf(",");
        }
        printf("{\"idx\":%d,\"name\":", i);
        json_escape_print(stdout, name);
        printf(",\"value\":");
        print_mms_json(MmsValue_getElement(values, i));
        printf("}");
    }
    printf("]}\n");
    fflush(stdout);
    if (dir) {
        LinkedList_destroy(dir);
    }
    ClientDataSet_destroy(ds);
}

static void cmd_enable_rcb(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char rcbGetRef[512];
    char handlerRef[512];
    char dsRef[512];
    json_str(line, "rcbGetRef", rcbGetRef, sizeof(rcbGetRef));
    json_str(line, "handlerRef", handlerRef, sizeof(handlerRef));
    json_str(line, "dsRef", dsRef, sizeof(dsRef));
    if (rcbGetRef[0] == 0 || dsRef[0] == 0) {
        emit_error("missing rcbGetRef or dsRef");
        return;
    }
    if (handlerRef[0] == 0) {
        strncpy(handlerRef, rcbGetRef, sizeof(handlerRef) - 1);
        char* last = strrchr(handlerRef, '.');
        if (last && strlen(last) > 3) {
            last[strlen(last) - 2] = 0;
        }
    }

    ActiveRcb* existing = find_rcb_by_handler(handlerRef);
    if (existing) {
        emit_error("rcb already enabled");
        return;
    }
    ActiveRcb* slot = alloc_rcb_slot();
    if (!slot) {
        emit_error("too many active rcbs");
        return;
    }
    strncpy(slot->rcbGetRef, rcbGetRef, MAX_RCB_REF - 1);
    strncpy(slot->handlerRef, handlerRef, MAX_RCB_REF - 1);
    strncpy(slot->dsRef, dsRef, MAX_RCB_REF - 1);

    IedClientError err = IED_ERROR_OK;
    slot->dataSetDirectory = IedConnection_getDataSetDirectory(g_con, &err, dsRef, NULL);
    slot->clientDataSet = IedConnection_readDataSetValues(g_con, &err, dsRef, NULL);
    if (err != IED_ERROR_OK || !slot->clientDataSet) {
        free_rcb_slot(slot);
        emit_error(ied_error_message(err));
        return;
    }

    slot->rcb = IedConnection_getRCBValues(g_con, &err, rcbGetRef, NULL);
    if (err != IED_ERROR_OK || !slot->rcb) {
        free_rcb_slot(slot);
        emit_error(ied_error_message(err));
        return;
    }

    if (!ClientReportControlBlock_isBuffered(slot->rcb)) {
        ClientReportControlBlock_setResv(slot->rcb, true);
    }
    ClientReportControlBlock_setTrgOps(
        slot->rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_GI);
    ClientReportControlBlock_setDataSetReference(slot->rcb, dsRef);
    ClientReportControlBlock_setRptEna(slot->rcb, true);
    ClientReportControlBlock_setGI(slot->rcb, true);

    IedConnection_installReportHandler(
        g_con, handlerRef, ClientReportControlBlock_getRptId(slot->rcb), reportCallback, slot);

    uint32_t mask = RCB_ELEMENT_DATSET | RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_RPT_ENA | RCB_ELEMENT_GI;
    if (!ClientReportControlBlock_isBuffered(slot->rcb)) {
        mask |= RCB_ELEMENT_RESV;
    }
    IedConnection_setRCBValues(g_con, &err, slot->rcb, mask, true);
    if (err != IED_ERROR_OK) {
        free_rcb_slot(slot);
        emit_error(ied_error_message(err));
        return;
    }

    printf("{\"evt\":\"rcb_enabled\",\"handlerRef\":");
    json_escape_print(stdout, handlerRef);
    printf(",\"rcbGetRef\":");
    json_escape_print(stdout, rcbGetRef);
    printf("}\n");
    fflush(stdout);
}

static void cmd_disable_rcb(const char* line) {
    char handlerRef[512];
    json_str(line, "handlerRef", handlerRef, sizeof(handlerRef));
    if (handlerRef[0] == 0) {
        emit_error("missing handlerRef");
        return;
    }
    ActiveRcb* slot = find_rcb_by_handler(handlerRef);
    if (!slot) {
        emit_error("rcb not found");
        return;
    }
    if (g_con && slot->rcb) {
        IedClientError err = IED_ERROR_OK;
        ClientReportControlBlock_setRptEna(slot->rcb, false);
        IedConnection_setRCBValues(g_con, &err, slot->rcb, RCB_ELEMENT_RPT_ENA, true);
    }
    free_rcb_slot(slot);
    emit_ok("disable_rcb");
}

static void cmd_gi(const char* line) {
    char handlerRef[512];
    json_str(line, "handlerRef", handlerRef, sizeof(handlerRef));
    ActiveRcb* slot = find_rcb_by_handler(handlerRef);
    if (!slot || !g_con || !slot->rcb) {
        emit_error("rcb not enabled");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    ClientReportControlBlock_setGI(slot->rcb, true);
    IedConnection_setRCBValues(g_con, &err, slot->rcb, RCB_ELEMENT_GI, true);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        return;
    }
    emit_ok("gi");
}

static void cmd_list_files(const char* line) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    char path[512];
    json_str(line, "path", path, sizeof(path));
    if (path[0] == 0) {
        strcpy(path, "/COMTRADE/");
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getFileDirectory(g_con, &err, path);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        return;
    }
    printf("{\"evt\":\"files\",\"path\":");
    json_escape_print(stdout, path);
    printf(",\"entries\":[");
    LinkedList el = list;
    int first = 1;
    while (el) {
        FileDirectoryEntry e = (FileDirectoryEntry)el->data;
        if (!first) {
            printf(",");
        }
        first = 0;
        printf("{\"name\":");
        json_escape_print(stdout, FileDirectoryEntry_getFileName(e));
        printf(",\"size\":%u,\"mtime\":%llu}",
               FileDirectoryEntry_getFileSize(e),
               (unsigned long long)FileDirectoryEntry_getLastModified(e));
        el = el->next;
    }
    printf("]}\n");
    fflush(stdout);
    if (list) {
        LinkedList_destroyDeep(list, (LinkedListValueDeleteFunction)FileDirectoryEntry_destroy);
    }
}

static void dispatch(const char* line) {
    char cmd[64];
    json_str(line, "cmd", cmd, sizeof(cmd));
    if (cmd[0] == 0) {
        emit_error("missing cmd");
        return;
    }
    if (strcmp(cmd, "connect") == 0) {
        cmd_connect(line);
    } else if (strcmp(cmd, "disconnect") == 0) {
        cmd_disconnect();
    } else if (strcmp(cmd, "read_comm_state") == 0) {
        cmd_read_comm_state(line);
    } else if (strcmp(cmd, "get_ld_list") == 0) {
        cmd_get_ld_list();
    } else if (strcmp(cmd, "get_datasets") == 0) {
        cmd_get_datasets(line);
    } else if (strcmp(cmd, "get_dataset_directory") == 0) {
        cmd_get_dataset_directory(line);
    } else if (strcmp(cmd, "read_dataset") == 0) {
        cmd_read_dataset(line);
    } else if (strcmp(cmd, "enable_rcb") == 0) {
        cmd_enable_rcb(line);
    } else if (strcmp(cmd, "disable_rcb") == 0) {
        cmd_disable_rcb(line);
    } else if (strcmp(cmd, "gi") == 0) {
        cmd_gi(line);
    } else if (strcmp(cmd, "list_files") == 0) {
        cmd_list_files(line);
    } else if (strcmp(cmd, "quit") == 0) {
        cmd_disconnect();
        printf("{\"evt\":\"bye\"}\n");
        fflush(stdout);
        exit(0);
    } else {
        emit_error("unknown cmd");
    }
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    char line[MAX_LINE];
    printf("{\"evt\":\"ready\"}\n");
    fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = 0;
        }
        if (len == 0) {
            continue;
        }
        dispatch(line);
    }
    cmd_disconnect();
    return 0;
}
