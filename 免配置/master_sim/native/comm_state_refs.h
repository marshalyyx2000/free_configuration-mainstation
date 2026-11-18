#ifndef COMM_STATE_REFS_H
#define COMM_STATE_REFS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMM_STATE_MAX_FCDA 256
#define COMM_STATE_LD_LEN 80
#define COMM_STATE_IED_LEN 80
#define COMM_STATE_OBJ_LEN 384

typedef struct {
    char ld[COMM_STATE_LD_LEN];
    char ied[COMM_STATE_IED_LEN];
    char ln[80];
    char doName[80];
    char daName[40];
    char fc[8];
} CommStateFcda;

/* E1Q1SB31LD0 / E1Q1SB31PROT → E1Q1SB31 */
void comm_state_ied_from_ld(const char* ld, char* ied, size_t n);

/* LD0 逻辑设备对应的保护 LD：E1Q1SB31LD0 → E1Q1SB31PROT */
int comm_state_prot_ld_from_ld0(const char* ld, char* protLd, size_t n);

/* IEC 对象引用，点号分隔，供 libiec61850 在第一处 '.' 插入 $FC$。
 * 例：E1Q1SB31LD0/LPHD1.Proxy.stVal */
int comm_state_build_object_ref(char* out, size_t outsz, const char* ld, const char* ln,
    const char* doName, const char* daName);

/* 已含 FC 的 MMS item，配合 IEC61850_FC_NONE 使用。
 * 例：LPHD1$ST$Proxy$stVal */
int comm_state_build_mms_item(char* out, size_t outsz, const char* ln, const char* fc,
    const char* doName, const char* daName);

/* 完整 MMS 对象：ld/LPHD1$ST$Proxy$stVal */
int comm_state_build_mms_object_ref(char* out, size_t outsz, const char* ld, const char* ln,
    const char* fc, const char* doName, const char* daName);

/* item 已是 LPHD1$ST$DO$DA 形式（第二段为两字母 FC） */
int comm_state_item_has_embedded_fc(const char* item);

/* LPHD1$Proxy$stVal + FC_ST 会被拼成 LPHD1$Proxy$stVal$ST，读必失败 */
int comm_state_dollar_ref_unsafe_with_fc(const char* objectReference);

void comm_state_fcda_set_defaults(CommStateFcda* fcda);

/* LPHD.Proxy（旧）还是 SDDI.ComStatus（保信标准） */
int comm_state_fcda_is_proxy(const CommStateFcda* fcda);
int comm_state_fcda_is_comstatus(const CommStateFcda* fcda);

int comm_state_parse_smcd_fcdas(const char* path, CommStateFcda* out, int maxOut);

#ifdef __cplusplus
}
#endif

#endif
