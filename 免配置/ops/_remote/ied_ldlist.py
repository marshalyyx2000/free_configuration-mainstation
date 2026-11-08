"""在子站上用 libiec61850 客户端直连装置，列出真实 LD/数据集/RCB。

router 代理转发后装置回 OBJECT_DOES_NOT_EXIST，需要确认装置实际模型
（LD inst、DataSet 名、ReportControl 实例名），据此校正主站引用。
"""
import ctypes
import ctypes.util
import glob
import os
import sys

CANDIDATES = glob.glob("/home/admin/router/lib/libiec61850.so*") \
    + glob.glob("/home/admin/src/configuration-free/iec61850_router/build/libiec61850/src/libiec61850.so") \
    + glob.glob("/usr/lib/libiec61850.so*")
print("lib candidates:", CANDIDATES)
if not CANDIDATES:
    sys.exit("no libiec61850 found")

lib = ctypes.CDLL(CANDIDATES[0])
lib.IedConnection_create.restype = ctypes.c_void_p
lib.IedConnection_connect.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_char_p, ctypes.c_int]
lib.IedConnection_getLogicalDeviceList.restype = ctypes.c_void_p
lib.IedConnection_getLogicalDeviceList.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
lib.LinkedList_destroy.argtypes = [ctypes.c_void_p]
lib.IedConnection_destroy.argtypes = [ctypes.c_void_p]
lib.IedConnection_close.argtypes = [ctypes.c_void_p]

con = lib.IedConnection_create()
err = ctypes.c_int(0)
lib.IedConnection_setConnectTimeout(con, 5000)
lib.IedConnection_setRequestTimeout(con, 10000)
lib.IedConnection_connect(con, ctypes.byref(err), b"172.16.50.83", 102)
print("connect err:", err.value)
if err.value != 0:
    sys.exit(1)

lst = lib.IedConnection_getLogicalDeviceList(con, ctypes.byref(err))
print("ld list err:", err.value)

# LinkedList 结构: data, next
class Node(ctypes.Structure):
    pass
Node._fields_ = [("data", ctypes.c_void_p), ("next", ctypes.POINTER(Node))]

node = ctypes.cast(lst, ctypes.POINTER(Node))
names = []
while node:
    d = node.contents.data
    if not d:
        break
    names.append(ctypes.string_at(d).decode("utf-8", "replace"))
    node = node.contents.next
print("LDs:", names)

lib.LinkedList_destroy(lst)
lib.IedConnection_close(con)
lib.IedConnection_destroy(con)
