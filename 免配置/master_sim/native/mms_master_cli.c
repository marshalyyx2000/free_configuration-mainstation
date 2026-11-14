/*
 * mms_master_cli - JSONL stdin/stdout MMS master for master_sim GUI.
 */
#include "iec61850_client.h"
#include "iec61850_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef REASON_UNKNOWN
#undef REASON_UNKNOWN
#endif
static CRITICAL_SECTION g_stdout_lock;
static INIT_ONCE g_stdout_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK stdout_lock_init(PINIT_ONCE once, PVOID param, PVOID* ctx)
{
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_stdout_lock);
    return TRUE;
}
static void stdout_lock(void)
{
    InitOnceExecuteOnce(&g_stdout_once, stdout_lock_init, NULL, NULL);
    EnterCriticalSection(&g_stdout_lock);
}
static void stdout_unlock(void)
{
    LeaveCriticalSection(&g_stdout_lock);
}
#else
static void stdout_lock(void)
{
    flockfile(stdout);
}
static void stdout_unlock(void)
{
    funlockfile(stdout);
}
#endif

const char* ied_error_message(IedClientError error);

#define MAX_LINE 65536
#define MAX_RCB 1024
#define MAX_RCB_REF 280
#define MAX_DS_ITEMS 512
#define ITEM_LEN 128
#define CONNECT_TIMEOUT_MS 10000
#define REQUEST_TIMEOUT_MS 60000
#define FILE_REQUEST_TIMEOUT_MS 15000

static IedConnection g_con = NULL;

typedef struct {
    bool used;
    char rcbGetRef[MAX_RCB_REF];
    char handlerRef[MAX_RCB_REF];
    char dsRef[MAX_RCB_REF];
    ClientReportControlBlock rcb;
    ClientDataSet clientDataSet;
    LinkedList dataSetDirectory;
    MmsValue* prevDsValues;
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
    stdout_lock();
    printf("{\"evt\":\"error\",\"msg\":");
    json_escape_print(stdout, msg ? msg : "unknown");
    printf("}\n");
    fflush(stdout);
    stdout_unlock();
}

static void emit_disconnected_reason(const char* reason) {
    stdout_lock();
    printf("{\"evt\":\"disconnected\",\"reason\":");
    json_escape_print(stdout, reason ? reason : "unknown");
    printf("}\n");
    fflush(stdout);
    stdout_unlock();
}

static void close_connection_after_lost(const char* reason);

static void emit_ok_ref(const char* cmd, const char* handlerRef) {
    stdout_lock();
    printf("{\"evt\":\"ok\",\"cmd\":");
    json_escape_print(stdout, cmd);
    if (handlerRef && handlerRef[0]) {
        printf(",\"handlerRef\":");
        json_escape_print(stdout, handlerRef);
    }
    printf("}\n");
    fflush(stdout);
    stdout_unlock();
}

static void emit_ok(const char* cmd) {
    emit_ok_ref(cmd, NULL);
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

static void print_mms_json(MmsValue* val);

static int json_str_array(const char* line, const char* key, char out[][ITEM_LEN], int maxn) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* p = strstr(line, pattern);
    if (!p) {
        return 0;
    }
    p = strchr(p, ':');
    if (!p) {
        return 0;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '[') {
        return 0;
    }
    p++;
    int n = 0;
    while (*p && *p != ']' && n < maxn) {
        while (*p == ' ' || *p == '\t' || *p == ',') {
            p++;
        }
        if (*p == ']' || !*p) {
            break;
        }
        if (*p != '"') {
            break;
        }
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < ITEM_LEN) {
            if (*p == '\\' && p[1]) {
                p++;
            }
            out[n][i++] = *p++;
        }
        out[n][i] = 0;
        if (*p == '"') {
            p++;
        }
        if (out[n][0]) {
            n++;
        }
    }
    return n;
}

static bool parse_dataset_ref(const char* ref, char* domain, size_t dsz, char* item, size_t isz) {
    if (!ref || !ref[0] || !domain || !item) {
        return false;
    }
    const char* slash = strchr(ref, '/');
    if (!slash || slash == ref) {
        return false;
    }
    size_t dlen = (size_t)(slash - ref);
    if (dlen >= dsz) {
        return false;
    }
    memcpy(domain, ref, dlen);
    domain[dlen] = 0;
    strncpy(item, slash + 1, isz - 1);
    item[isz - 1] = 0;
    for (char* p = item; *p; p++) {
        if (*p == '.') {
            *p = '$';
        }
    }
    return domain[0] && item[0];
}

static void iec_addr_to_mms_item(const char* addr, char* item, size_t isz) {
    item[0] = 0;
    if (!addr || !addr[0]) {
        return;
    }
    const char* s = strchr(addr, '/');
    s = s ? s + 1 : addr;
    char tmp[256];
    strncpy(tmp, s, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    if (strchr(tmp, '$') && !strchr(tmp, '[')) {
        strncpy(item, tmp, isz - 1);
        item[isz - 1] = 0;
        return;
    }
    char fc[8] = {0};
    char* lb = strrchr(tmp, '[');
    char* rb = strrchr(tmp, ']');
    if (lb && rb && rb > lb) {
        size_t n = (size_t)(rb - lb - 1);
        if (n > 0 && n < sizeof(fc)) {
            memcpy(fc, lb + 1, n);
            fc[n] = 0;
        }
        *lb = 0;
    }
    char* dot = strchr(tmp, '.');
    if (!dot) {
        strncpy(item, tmp, isz - 1);
        item[isz - 1] = 0;
        return;
    }
    *dot = 0;
    char* path = dot + 1;
    for (char* p = path; *p; p++) {
        if (*p == '.') {
            *p = '$';
        }
    }
    if (fc[0]) {
        snprintf(item, isz, "%s$%s$%s", tmp, fc, path);
    } else {
        snprintf(item, isz, "%s$%s", tmp, path);
    }
}

static const char* mms_error_text(MmsError err) {
    switch (err) {
    case MMS_ERROR_NONE:
        return "OK";
    case MMS_ERROR_SERVICE_TIMEOUT:
        return "TIMEOUT";
    case MMS_ERROR_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case MMS_ERROR_ACCESS_OBJECT_NON_EXISTENT:
        return "OBJECT_DOES_NOT_EXIST";
    case MMS_ERROR_ACCESS_OBJECT_ACCESS_DENIED:
        return "ACCESS_DENIED";
    case MMS_ERROR_ACCESS_OBJECT_ACCESS_UNSUPPORTED:
        return "OBJECT_ACCESS_UNSUPPORTED";
    default: {
        static char buf[32];
        snprintf(buf, sizeof(buf), "MMS_%d", (int)err);
        return buf;
    }
    }
}

static void emit_after_ied_error(IedClientError err) {
    if (err == IED_ERROR_CONNECTION_LOST || err == IED_ERROR_NOT_CONNECTED) {
        close_connection_after_lost(ied_error_message(err));
        return;
    }
    emit_error(ied_error_message(err));
}

static void emit_after_mms_error(MmsError err) {
    const char* msg = mms_error_text(err);
    if (err == MMS_ERROR_CONNECTION_LOST) {
        close_connection_after_lost(msg);
        return;
    }
    emit_error(msg);
}

static void apply_cmd_timeout(const char* line) {
    if (!g_con || !line) {
        return;
    }
    int ms = json_int(line, "timeoutMs", 0);
    if (ms >= 1000) {
        IedConnection_setRequestTimeout(g_con, ms);
    }
}

static MmsValue* read_dataset_fast(const char* ref, const char* line, MmsError* outErr) {
    if (outErr) {
        *outErr = MMS_ERROR_INVALID_ARGUMENTS;
    }
    char domain[65];
    char listName[129];
    if (!g_con || !parse_dataset_ref(ref, domain, sizeof(domain), listName, sizeof(listName))) {
        return NULL;
    }
    apply_cmd_timeout(line);

    if (strchr(ref, '$')) {
        IedClientError iedErr = IED_ERROR_OK;
        ClientDataSet cds = IedConnection_readDataSetValues(g_con, &iedErr, ref, NULL);
        if (cds && iedErr == IED_ERROR_OK) {
            MmsValue* raw = ClientDataSet_getValues(cds);
            MmsValue* values = raw ? MmsValue_clone(raw) : NULL;
            ClientDataSet_destroy(cds);
            if (values) {
                if (outErr) {
                    *outErr = MMS_ERROR_NONE;
                }
                return values;
            }
        } else if (cds) {
            ClientDataSet_destroy(cds);
        }
        if (iedErr == IED_ERROR_CONNECTION_LOST || iedErr == IED_ERROR_NOT_CONNECTED) {
            close_connection_after_lost(ied_error_message(iedErr));
            if (outErr) {
                *outErr = MMS_ERROR_CONNECTION_LOST;
            }
            return NULL;
        }
    }

    MmsConnection mms = IedConnection_getMmsConnection(g_con);
    if (!mms) {
        return NULL;
    }

    MmsError err = MMS_ERROR_NONE;
    /* 直连装置时常用 Read 命名数据集；子站合并模型优先走 readDataSetValues。 */
    MmsValue* values = MmsConnection_readNamedVariableListValues(mms, &err, domain, listName, false);
    if (values) {
        if (outErr) {
            *outErr = MMS_ERROR_NONE;
        }
        return values;
    }

    char items[MAX_DS_ITEMS][ITEM_LEN];
    char mmsItems[MAX_DS_ITEMS][ITEM_LEN];
    int n = line ? json_str_array(line, "items", items, MAX_DS_ITEMS) : 0;
    if (n <= 0) {
        if (outErr) {
            *outErr = err;
        }
        return NULL;
    }

    LinkedList list = LinkedList_create();
    for (int i = 0; i < n; i++) {
        iec_addr_to_mms_item(items[i], mmsItems[i], ITEM_LEN);
        if (mmsItems[i][0]) {
            LinkedList_add(list, mmsItems[i]);
        }
    }
    values = MmsConnection_readMultipleVariables(mms, &err, domain, list);
    LinkedList_destroyStatic(list);
    if (outErr) {
        *outErr = err;
    }
    return values;
}

static void emit_dataset_values(const char* ref, MmsValue* values) {
    int n = 0;
    int asArray = 0;
    if (values && (MmsValue_getType(values) == MMS_ARRAY || MmsValue_getType(values) == MMS_STRUCTURE)) {
        n = MmsValue_getArraySize(values);
        asArray = 1;
    } else if (values) {
        n = 1;
    }
    stdout_lock();
    printf("{\"evt\":\"dataset\",\"ref\":");
    json_escape_print(stdout, ref);
    printf(",\"rows\":[");
    for (int i = 0; i < n; i++) {
        if (i) {
            printf(",");
        }
        printf("{\"idx\":%d,\"name\":\"\",\"value\":", i);
        print_mms_json(asArray ? MmsValue_getElement(values, i) : values);
        printf("}");
    }
    printf("]}\n");
    fflush(stdout);
    stdout_unlock();
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

static ActiveRcb* find_rcb_by_handler(const char* handlerRef) {
    for (int i = 0; i < MAX_RCB; i++) {
        if (g_rcbs[i].used && strcmp(g_rcbs[i].handlerRef, handlerRef) == 0) {
            return &g_rcbs[i];
        }
    }
    return NULL;
}

static const char* skip_ds_at(const char* s)
{
    if (s && s[0] == '@')
        return s + 1;
    return s;
}

static bool same_ds_ref(const char* a, const char* b)
{
    a = skip_ds_at(a);
    b = skip_ds_at(b);
    return a && b && a[0] && strcmp(a, b) == 0;
}

static ActiveRcb* find_rcb_by_dataset(const char* dsRef)
{
    if (!dsRef || !dsRef[0])
        return NULL;
    for (int i = 0; i < MAX_RCB; i++) {
        if (g_rcbs[i].used && same_ds_ref(g_rcbs[i].dsRef, dsRef))
            return &g_rcbs[i];
    }
    return NULL;
}

static bool report_item_value_changed(MmsValue* cur, MmsValue* prev) {
    if (!cur) {
        return false;
    }
    if (!prev) {
        return true;
    }
    bool a = false;
    bool b = false;
    if (comm_stval_from_item(cur, &a) && comm_stval_from_item(prev, &b)) {
        return a != b;
    }
    char bufA[512];
    char bufB[512];
    bufA[0] = 0;
    bufB[0] = 0;
    MmsValue_printToBuffer(cur, bufA, (int)sizeof(bufA));
    MmsValue_printToBuffer(prev, bufB, (int)sizeof(bufB));
    return strcmp(bufA, bufB) != 0;
}

static void snapshot_report_values(ActiveRcb* slot, MmsValue* dataSetValues) {
    if (!slot || !dataSetValues) {
        return;
    }
    if (slot->prevDsValues) {
        MmsValue_delete(slot->prevDsValues);
        slot->prevDsValues = NULL;
    }
    slot->prevDsValues = MmsValue_clone(dataSetValues);
}

static void reportCallback(void* parameter, ClientReport report) {
    ActiveRcb* slot = (ActiveRcb*)parameter;
    if (!slot || !report) {
        return;
    }

    /*
     * A server may reuse one RptID for several RCBs (the configuration-free
     * substation rewrites rptID="NULL" to "<IED>NULL"). libiec61850 then
     * dispatches the report to the first handler installed for that RptID.
     * Re-dispatch by the report's DatSet so values, previous snapshot and
     * RCB reference belong to the data set that actually changed.
     * DatSet in the report is often prefixed with '@'.
     */
    ActiveRcb* target = slot;
    const char* dataSetName = ClientReport_hasDataSetName(report)
        ? ClientReport_getDataSetName(report) : NULL;
    ActiveRcb* byDs = find_rcb_by_dataset(dataSetName);
    if (byDs)
        target = byDs;

    MmsValue* dataSetValues = ClientReport_getDataSetValues(report);
    const char* rcbRef = target->rcbGetRef[0] ? target->rcbGetRef : slot->rcbGetRef;
    const char* dsOut = (dataSetName && dataSetName[0]) ? skip_ds_at(dataSetName)
                                                       : (target->dsRef[0] ? target->dsRef : "");
    const bool hasReason = ClientReport_hasReasonForInclusion(report);

    stdout_lock();
    printf("{\"evt\":\"report\",\"rcb\":");
    json_escape_print(stdout, rcbRef ? rcbRef : "");
    printf(",\"ds\":");
    json_escape_print(stdout, dsOut ? dsOut : "");
    printf(",\"items\":[");
    int first = 1;
    int n = dataSetValues ? MmsValue_getArraySize(dataSetValues) : 0;
    for (int i = 0; i < n; i++) {
        ReasonForInclusion reason = hasReason
            ? ClientReport_getReasonForInclusion(report, i)
            : IEC61850_REASON_GI;
        MmsValue* curVal = dataSetValues ? MmsValue_getElement(dataSetValues, i) : NULL;
        MmsValue* prevVal = (target->prevDsValues && dataSetValues) ? MmsValue_getElement(target->prevDsValues, i) : NULL;
        if (reason == IEC61850_REASON_NOT_INCLUDED) {
            /* Vendor 突发性 (e.g. reason code 0x40) may not map to IEC61850_REASON_DATA_CHANGE in the stack. */
            if (curVal && report_item_value_changed(curVal, prevVal)) {
                reason = IEC61850_REASON_DATA_CHANGE;
            } else {
                continue;
            }
        }
        if (!first) {
            printf(",");
        }
        first = 0;
        const char* name = "";
        if (ClientReport_hasDataReference(report)) {
            const char* dr = ClientReport_getDataReference(report, i);
            if (dr && dr[0]) {
                name = dr;
            }
        }
        if (name[0] == 0 && target->dataSetDirectory) {
            LinkedList entry = LinkedList_get(target->dataSetDirectory, i);
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
    stdout_unlock();
    snapshot_report_values(target, dataSetValues);
}

static void free_rcb_slot(ActiveRcb* slot) {
    if (!slot || !slot->used) {
        return;
    }
    if (g_con) {
        IedConnection_uninstallReportHandler(g_con, slot->rcbGetRef[0] ? slot->rcbGetRef : slot->handlerRef);
    }
    if (slot->rcb) {
        ClientReportControlBlock_destroy(slot->rcb);
        slot->rcb = NULL;
    }
    if (slot->clientDataSet) {
        ClientDataSet_destroy(slot->clientDataSet);
        slot->clientDataSet = NULL;
    }
    if (slot->prevDsValues) {
        MmsValue_delete(slot->prevDsValues);
        slot->prevDsValues = NULL;
    }
    if (slot->dataSetDirectory) {
        LinkedList_destroy(slot->dataSetDirectory);
        slot->dataSetDirectory = NULL;
    }
    slot->used = false;
}

static void close_connection_after_lost(const char* reason) {
    for (int i = 0; i < MAX_RCB; i++) {
        free_rcb_slot(&g_rcbs[i]);
    }
    if (g_con) {
        IedConnection_close(g_con);
        IedConnection_destroy(g_con);
        g_con = NULL;
    }
    emit_error(reason ? reason : "CONNECTION_LOST");
    emit_disconnected_reason(reason ? reason : "CONNECTION_LOST");
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
        IedConnection_close(g_con);
        IedConnection_destroy(g_con);
        g_con = NULL;
    }
    for (int i = 0; i < MAX_RCB; i++) {
        free_rcb_slot(&g_rcbs[i]);
    }

    int reqTimeout = json_int(line, "timeoutMs", REQUEST_TIMEOUT_MS);
    if (reqTimeout < 1000) {
        reqTimeout = REQUEST_TIMEOUT_MS;
    }
    g_con = IedConnection_create();
    IedConnection_setConnectTimeout(g_con, CONNECT_TIMEOUT_MS);
    IedConnection_setRequestTimeout(g_con, reqTimeout);
    IedClientError err = IED_ERROR_OK;
    IedConnection_connect(g_con, &err, host, port);
    if (err != IED_ERROR_OK) {
        emit_error(ied_error_message(err));
        IedConnection_destroy(g_con);
        g_con = NULL;
        return;
    }
    stdout_lock();
    printf("{\"evt\":\"connected\",\"host\":");
    json_escape_print(stdout, host);
    printf(",\"port\":%d}\n", port);
    fflush(stdout);
    stdout_unlock();
}

static void cmd_disconnect_with_reason(const char* reason) {
    for (int i = 0; i < MAX_RCB; i++) {
        free_rcb_slot(&g_rcbs[i]);
    }
    if (g_con) {
        IedConnection_close(g_con);
        IedConnection_destroy(g_con);
        g_con = NULL;
    }
    emit_disconnected_reason(reason ? reason : "user");
}

static void cmd_ping(void) {
    if (!g_con) {
        emit_error("NOT_CONNECTED");
        emit_disconnected_reason("NOT_CONNECTED");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getLogicalDeviceList(g_con, &err);
    if (err != IED_ERROR_OK) {
        if (list) {
            LinkedList_destroy(list);
        }
        emit_after_ied_error(err);
        return;
    }
    if (list) {
        LinkedList_destroy(list);
    }
    stdout_lock();
    printf("{\"evt\":\"ok\",\"cmd\":\"ping\"}\n");
    fflush(stdout);
    stdout_unlock();
}

static void cmd_disconnect(void) {
    cmd_disconnect_with_reason("user");
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
    apply_cmd_timeout(line);
    MmsError mmsErr = MMS_ERROR_NONE;
    MmsValue* values = read_dataset_fast(dsRef, line, &mmsErr);
    if (!values) {
        emit_after_mms_error(mmsErr);
        return;
    }
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
    MmsValue_delete(values);
}

static void cmd_get_ld_list(void) {
    if (!g_con) {
        emit_error("not connected");
        return;
    }
    IedClientError err = IED_ERROR_OK;
    LinkedList list = IedConnection_getLogicalDeviceList(g_con, &err);
    if (err != IED_ERROR_OK) {
        emit_after_ied_error(err);
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
        emit_after_ied_error(err);
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
        emit_after_ied_error(err);
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

static bool force_release_rcb_at_get_ref(const char* rcbGetRef)
{
    if (!g_con || !rcbGetRef || rcbGetRef[0] == 0)
        return false;
    IedClientError err = IED_ERROR_OK;
    ClientReportControlBlock rcb = IedConnection_getRCBValues(g_con, &err, rcbGetRef, NULL);
    if (err != IED_ERROR_OK || !rcb)
        return false;
    if (!ClientReportControlBlock_isBuffered(rcb))
        ClientReportControlBlock_setResv(rcb, true);
    ClientReportControlBlock_setRptEna(rcb, false);
    IedConnection_setRCBValues(g_con, &err, rcb, RCB_ELEMENT_RPT_ENA, true);
    ClientReportControlBlock_destroy(rcb);
    return err == IED_ERROR_OK;
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
    MmsError mmsErr = MMS_ERROR_NONE;
    MmsValue* values = read_dataset_fast(ref, line, &mmsErr);
    if (!values) {
        emit_after_mms_error(mmsErr);
        return;
    }
    emit_dataset_values(ref, values);
    MmsValue_delete(values);
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
        printf("{\"evt\":\"rcb_enabled\",\"handlerRef\":");
        json_escape_print(stdout, handlerRef);
        printf(",\"rcbGetRef\":");
        json_escape_print(stdout, existing->rcbGetRef);
        printf(",\"already\":true}\n");
        fflush(stdout);
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
    slot->dataSetDirectory = NULL;
    slot->clientDataSet = NULL;

    slot->rcb = IedConnection_getRCBValues(g_con, &err, rcbGetRef, NULL);
    if (err != IED_ERROR_OK || !slot->rcb) {
        free_rcb_slot(slot);
        emit_after_ied_error(err);
        return;
    }

    if (!ClientReportControlBlock_isBuffered(slot->rcb)) {
        ClientReportControlBlock_setResv(slot->rcb, true);
    }

    slot->clientDataSet = IedConnection_readDataSetValues(g_con, &err, dsRef, NULL);
    if (err != IED_ERROR_OK || !slot->clientDataSet) {
        free_rcb_slot(slot);
        emit_after_ied_error(err);
        return;
    }
    slot->dataSetDirectory = IedConnection_getDataSetDirectory(g_con, &err, dsRef, NULL);

    if (slot->clientDataSet) {
        MmsValue* seed = ClientDataSet_getValues(slot->clientDataSet);
        if (seed) {
            snapshot_report_values(slot, seed);
        }
    }

    ClientReportControlBlock_setTrgOps(
        slot->rcb, TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_GI);
    ClientReportControlBlock_setDataSetReference(slot->rcb, dsRef);
    ClientReportControlBlock_setOptFlds(slot->rcb,
        RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_SET
            | RPT_OPT_DATA_REFERENCE | RPT_OPT_CONF_REV);
    ClientReportControlBlock_setRptEna(slot->rcb, true);
    ClientReportControlBlock_setGI(slot->rcb, true);

    IedConnection_installReportHandler(
        g_con, slot->rcbGetRef, ClientReportControlBlock_getRptId(slot->rcb), reportCallback, slot);

    uint32_t mask = RCB_ELEMENT_DATSET | RCB_ELEMENT_TRG_OPS | RCB_ELEMENT_OPT_FLDS | RCB_ELEMENT_RPT_ENA
        | RCB_ELEMENT_GI;
    if (!ClientReportControlBlock_isBuffered(slot->rcb)) {
        mask |= RCB_ELEMENT_RESV;
    }
    IedConnection_setRCBValues(g_con, &err, slot->rcb, mask, true);
    if (err != IED_ERROR_OK) {
        mask &= ~RCB_ELEMENT_OPT_FLDS;
        err = IED_ERROR_OK;
        IedConnection_setRCBValues(g_con, &err, slot->rcb, mask, true);
    }
    if (err == IED_ERROR_TEMPORARILY_UNAVAILABLE) {
        (void)force_release_rcb_at_get_ref(rcbGetRef);
        err = IED_ERROR_OK;
        IedConnection_setRCBValues(g_con, &err, slot->rcb, mask, true);
    }
    if (err != IED_ERROR_OK) {
        free_rcb_slot(slot);
        emit_after_ied_error(err);
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
    char rcbGetRef[512];
    json_str(line, "handlerRef", handlerRef, sizeof(handlerRef));
    json_str(line, "rcbGetRef", rcbGetRef, sizeof(rcbGetRef));
    if (handlerRef[0] == 0) {
        emit_error("missing handlerRef");
        return;
    }
    ActiveRcb* slot = find_rcb_by_handler(handlerRef);
    if (!slot) {
        if (rcbGetRef[0] != 0 && force_release_rcb_at_get_ref(rcbGetRef)) {
            emit_ok_ref("disable_rcb", handlerRef);
            return;
        }
        emit_error("rcb not found");
        return;
    }
    if (g_con && slot->rcb) {
        IedClientError err = IED_ERROR_OK;
        ClientReportControlBlock_setRptEna(slot->rcb, false);
        IedConnection_setRCBValues(g_con, &err, slot->rcb, RCB_ELEMENT_RPT_ENA, true);
    }
    free_rcb_slot(slot);
    emit_ok_ref("disable_rcb", handlerRef);
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
        emit_after_ied_error(err);
        return;
    }
    emit_ok_ref("gi", handlerRef);
}

static void emit_files_json(const char* path, LinkedList list, const char* warn) {
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
    printf("]");
    if (warn && warn[0]) {
        printf(",\"warn\":");
        json_escape_print(stdout, warn);
    }
    printf("}\n");
    fflush(stdout);
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
    IedConnection_setRequestTimeout(g_con, FILE_REQUEST_TIMEOUT_MS);

    const char* try_paths[4];
    int n_try = 0;
    try_paths[n_try++] = path;
    if (strcmp(path, "/COMTRADE/") != 0) {
        try_paths[n_try++] = "/COMTRADE/";
    }
    if (strcmp(path, "COMTRADE/") != 0) {
        try_paths[n_try++] = "COMTRADE/";
    }
    if (strcmp(path, "/") != 0) {
        try_paths[n_try++] = "/";
    }

    IedClientError err = IED_ERROR_OK;
    LinkedList list = NULL;
    const char* used_path = path;
    for (int i = 0; i < n_try; i++) {
        err = IED_ERROR_OK;
        if (list) {
            LinkedList_destroyDeep(list, (LinkedListValueDeleteFunction)FileDirectoryEntry_destroy);
            list = NULL;
        }
        list = IedConnection_getFileDirectory(g_con, &err, try_paths[i]);
        if (err == IED_ERROR_OK) {
            used_path = try_paths[i];
            break;
        }
    }

    IedConnection_setRequestTimeout(g_con, REQUEST_TIMEOUT_MS);

    if (err == IED_ERROR_OK) {
        emit_files_json(used_path, list, NULL);
        if (list) {
            LinkedList_destroyDeep(list, (LinkedListValueDeleteFunction)FileDirectoryEntry_destroy);
        }
        return;
    }

    if (list) {
        LinkedList_destroyDeep(list, (LinkedListValueDeleteFunction)FileDirectoryEntry_destroy);
        list = NULL;
    }
    /* 部分装置文件目录响应极慢或不实现：返回空列表并标注原因，避免录波页/测试误判为硬失败 */
    if (err == IED_ERROR_TIMEOUT || err == IED_ERROR_SERVICE_NOT_SUPPORTED
        || err == IED_ERROR_OBJECT_ACCESS_UNSUPPORTED) {
        emit_files_json(path, NULL, ied_error_message(err));
        return;
    }
    emit_after_ied_error(err);
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
    } else if (strcmp(cmd, "ping") == 0) {
        cmd_ping();
    } else if (strcmp(cmd, "quit") == 0) {
        cmd_disconnect_with_reason("quit");
        stdout_lock();
        printf("{\"evt\":\"bye\"}\n");
        fflush(stdout);
        stdout_unlock();
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
    cmd_disconnect_with_reason("stdin_eof");
    return 0;
}
