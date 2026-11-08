"""修补 libiec61850 服务端代理分发：
本地模型（本 IED 的 MmsDomain）存在的 domain 必须本地处理，只有本地不存在的
domain 才转发给下联装置。否则如 `LD0`、`P_X5071APROT` 等本地域也会被当作
代理请求，匹配不到装置就被丢弃，客户端永远等不到响应（表现为 TIMEOUT）。
"""

import os
import re
import sys

ROOT = "/home/admin/src/configuration-free/iec61850_router/third_party/libiec61850-1.5.3/src/mms/iso_mms/server"
READ = os.path.join(ROOT, "mms_read_service.c")
WRITE = os.path.join(ROOT, "mms_write_service.c")

HELPER = """
static int
domainExistsLocally(MmsServerConnection connection, const char* domainIdStr)
{
    MmsDevice* device = MmsServer_getDevice(connection->server);
    if (device == NULL)
        return 0;
    /* MmsDevice_getDomain 兼容 IED 名前缀匹配（SubStationLD0 属于 SubStation） */
    return MmsDevice_getDomain(device, (char*) domainIdStr) != NULL;
}
"""

OLD_NAMED = """        // check domainIdStr is start with deviceName
        if (strncmp(domainIdStr, connection->server->device->deviceName, strlen(connection->server->device->deviceName)) != 0) {
            if(connection->server->proxyReadNamedVariableHandler != NULL){
                response->size = 0;
                connection->server->proxyReadNamedVariableHandler(connection->server->proxyReadNamedVariableHandlerParameter,
                                                                connection->isoConnection, invokeId, domainIdStr, nameIdStr);
                return;
            }
        }
"""

NEW_NAMED = """        /* 本地模型存在的 domain 必须本地处理；只有本地没有的才代理给下联装置。 */
        if (!domainExistsLocally(connection, domainIdStr)) {
            if(connection->server->proxyReadNamedVariableHandler != NULL){
                response->size = 0;
                connection->server->proxyReadNamedVariableHandler(connection->server->proxyReadNamedVariableHandlerParameter,
                                                                connection->isoConnection, invokeId, domainIdStr, nameIdStr);
                return;
            }
        }
"""

OLD_READ = """    int referenceSize = LinkedList_size(domains);
    if(referenceSize > 0 && connection->server->proxyReadVariableHandler != NULL){
        connection->server->proxyReadVariableHandler(connection->server->proxyReadVariableHandlerParameter,
                                                    connection->isoConnection, invokeId, domains, names);
        goto exit;
    }
"""

NEW_READ = """    int referenceSize = LinkedList_size(domains);
    if(referenceSize > 0 && connection->server->proxyReadVariableHandler != NULL){
        int proxyAll = 1;
        LinkedList domainElem = LinkedList_getNext(domains);
        while (domainElem) {
            const char* d = (const char*) LinkedList_getData(domainElem);
            if (d != NULL && domainExistsLocally(connection, d)) {
                proxyAll = 0;
                break;
            }
            domainElem = LinkedList_getNext(domainElem);
        }
        if (proxyAll) {
            connection->server->proxyReadVariableHandler(connection->server->proxyReadVariableHandlerParameter,
                                                        connection->isoConnection, invokeId, domains, names);
            goto exit;
        }
    }
"""

OLD_WRITE = """        int reference = LinkedList_size(domains);
        if(reference > 0 && connection->server->proxyWriteVariableHandler != NULL){
            connection->server->proxyWriteVariableHandler(connection->server->proxyWriteVariableHandlerParameter, connection->isoConnection, invokeId, domains, names, values);
        }
"""

NEW_WRITE = """        int reference = LinkedList_size(domains);
        if(reference > 0 && connection->server->proxyWriteVariableHandler != NULL){
            int proxyAll = 1;
            LinkedList domainElem = LinkedList_getNext(domains);
            while (domainElem) {
                const char* d = (const char*) LinkedList_getData(domainElem);
                if (d != NULL && domainExistsLocally(connection, d)) {
                    proxyAll = 0;
                    break;
                }
                domainElem = LinkedList_getNext(domainElem);
            }
            if (proxyAll)
                connection->server->proxyWriteVariableHandler(connection->server->proxyWriteVariableHandlerParameter, connection->isoConnection, invokeId, domains, names, values);
            else if (sendResponse)
                mmsServer_createMmsWriteResponse(connection, invokeId, response, numberOfWriteItems, accessResults);
        }
"""


def ensure_helper(text: str) -> str:
    if "domainExistsLocally" in text:
        return text
    anchor = "static void\nhandleReadNamedVariableListRequest("
    idx = text.find(anchor)
    if idx < 0:
        raise RuntimeError("anchor for helper not found")
    return text[:idx] + HELPER.strip("\n") + "\n\n" + text[idx:]


def patch(path: str, pairs) -> bool:
    with open(path, "rb") as fh:
        raw = fh.read()
    text = raw.decode("utf-8").replace("\r\n", "\n")
    changed = False
    for old, new in pairs:
        if old not in text:
            continue
        text = text.replace(old, new, 1)
        changed = True
    if changed:
        with open(path, "wb") as fh:
            fh.write(text.encode("utf-8"))
    return changed


def main() -> int:
    with open(READ, "rb") as fh:
        rtext = fh.read().decode("utf-8").replace("\r\n", "\n")
    rtext = ensure_helper(rtext)
    with open(READ, "wb") as fh:
        fh.write(rtext.encode("utf-8"))

    rc1 = patch(READ, [(OLD_NAMED, NEW_NAMED), (OLD_READ, NEW_READ)])
    rc2 = patch(WRITE, [(OLD_WRITE, NEW_WRITE)])
    print("read patched:", rc1, "write patched:", rc2)
    return 0 if (rc1 or rc2) else 1


if __name__ == "__main__":
    sys.exit(main())
