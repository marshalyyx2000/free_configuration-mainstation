/*
 * 免配置子站 MMS 通信工况总召客户端（在装置或任意 Linux 上编译运行）。
 *
 *   gcc -O2 -o mms_gi mms_gi.c \
 *       -I<libiec61850>/src/iec61850/inc -I<libiec61850>/src/mms/inc \
 *       -I<libiec61850>/src/common/inc -I<libiec61850>/hal/inc -I<libiec61850>/src/logging \
 *       -L<build>/libiec61850/src -Wl,-rpath,<build>/libiec61850/src -liec61850 -lpthread
 *
 *   ./mms_gi [host] [port] [datasetRef]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "iec61850_client.h"

static int read_dataset(IedConnection con, const char* dsRef) {
    IedClientError error = IED_ERROR_OK;
    ClientDataSet dataSet = IedConnection_readDataSetValues(con, &error, dsRef, NULL);

    if (error != IED_ERROR_OK || dataSet == NULL) {
        printf("FAIL read %s error=%d (%s)\n", dsRef, error, IedClientError_getMessage(error));
        return 2;
    }

    MmsValue* values = ClientDataSet_getValues(dataSet);
    int n = MmsValue_getArraySize(values);
    printf("OK %s entries=%d\n", dsRef, n);

    int connected = 0;
    for (int i = 0; i < n; i++) {
        MmsValue* item = MmsValue_getElement(values, i);
        char buf[256];
        buf[0] = 0;
        MmsValue_printToBuffer(item, buf, (int)sizeof(buf));
        printf("%3d %s\n", i + 1, buf);

        /* 条目的类型是 Proxy 的 (stVal, q, t) 结构体，通信状态在第一个成员 */
        if (MmsValue_getType(item) == MMS_BOOLEAN) {
            if (MmsValue_getBoolean(item)) {
                connected++;
            }
        } else if (MmsValue_getType(item) == MMS_STRUCTURE && MmsValue_getArraySize(item) > 0) {
            MmsValue* stVal = MmsValue_getElement(item, 0);
            if (stVal != NULL && MmsValue_getType(stVal) == MMS_BOOLEAN && MmsValue_getBoolean(stVal)) {
                connected++;
            }
        }
    }
    printf("summary: %d/%d connected\n", connected, n);

    ClientDataSet_destroy(dataSet);
    return 0;
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    int port = argc > 2 ? atoi(argv[2]) : 102;
    const char* dsRef = argc > 3 ? argv[3] : "SubStationLD0/LLN0$dsCommState";

    IedConnection con = IedConnection_create();
    IedClientError error = IED_ERROR_OK;
    IedConnection_setRequestTimeout(con, 30000);

    IedConnection_connect(con, &error, host, port);
    if (error != IED_ERROR_OK) {
        printf("FAIL connect %s:%d error=%d (%s)\n", host, port, error, IedClientError_getMessage(error));
        IedConnection_destroy(con);
        return 1;
    }
    printf("connected %s:%d\n", host, port);

    int rc = read_dataset(con, dsRef);

    IedConnection_close(con);
    IedConnection_destroy(con);
    return rc;
}
