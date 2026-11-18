#include "comm_state_refs.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

static const char* kLdSuffixes[] = {
    "PIGO", "RPIT", "PROT", "MEAS", "CTRL", "RCD", "LD0", NULL
};

static FILE* open_utf8(const char* path)
{
#ifdef _WIN32
    wchar_t wpath[1024];
    int pn = MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 1024);
    if (pn == 0)
        pn = MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, 1024);
    if (pn == 0)
        return NULL;
    return _wfopen(wpath, L"rb");
#else
    return fopen(path, "rb");
#endif
}

static void copy_trunc(char* dst, size_t n, const char* src)
{
    if (!dst || n == 0)
        return;
    dst[0] = 0;
    if (!src)
        return;
    strncpy(dst, src, n - 1);
    dst[n - 1] = 0;
}

void comm_state_ied_from_ld(const char* ld, char* ied, size_t n)
{
    ied[0] = 0;
    if (!ld || !ld[0] || n < 2)
        return;
    size_t len = strlen(ld);
    size_t out = len;
    for (int i = 0; kLdSuffixes[i]; i++) {
        size_t sl = strlen(kLdSuffixes[i]);
        if (len > sl && strcmp(ld + len - (int)sl, kLdSuffixes[i]) == 0) {
            out = len - sl;
            break;
        }
    }
    if (out >= n)
        out = n - 1;
    memcpy(ied, ld, out);
    ied[out] = 0;
}

int comm_state_prot_ld_from_ld0(const char* ld, char* protLd, size_t n)
{
    protLd[0] = 0;
    if (!ld || n < 8)
        return 0;
    size_t len = strlen(ld);
    if (len < 4 || strcmp(ld + len - 3, "LD0") != 0)
        return 0;
    char ied[COMM_STATE_IED_LEN];
    comm_state_ied_from_ld(ld, ied, sizeof(ied));
    if (!ied[0])
        return 0;
    snprintf(protLd, n, "%sPROT", ied);
    return protLd[0] != 0;
}

int comm_state_build_object_ref(char* out, size_t outsz, const char* ld, const char* ln,
    const char* doName, const char* daName)
{
    if (!out || outsz < 8 || !ld || !ld[0] || !ln || !ln[0] || !doName || !doName[0]) {
        if (out && outsz)
            out[0] = 0;
        return 0;
    }
    const char* da = (daName && daName[0]) ? daName : "stVal";
    int n = snprintf(out, outsz, "%s/%s.%s.%s", ld, ln, doName, da);
    return n > 0 && (size_t)n < outsz;
}

int comm_state_build_mms_item(char* out, size_t outsz, const char* ln, const char* fc,
    const char* doName, const char* daName)
{
    if (!out || outsz < 8 || !ln || !ln[0] || !doName || !doName[0]) {
        if (out && outsz)
            out[0] = 0;
        return 0;
    }
    const char* f = (fc && fc[0]) ? fc : "ST";
    const char* da = (daName && daName[0]) ? daName : "stVal";
    int n = snprintf(out, outsz, "%s$%s$%s$%s", ln, f, doName, da);
    return n > 0 && (size_t)n < outsz;
}

int comm_state_build_mms_object_ref(char* out, size_t outsz, const char* ld, const char* ln,
    const char* fc, const char* doName, const char* daName)
{
    char item[192];
    if (!comm_state_build_mms_item(item, sizeof(item), ln, fc, doName, daName) || !ld || !ld[0]) {
        if (out && outsz)
            out[0] = 0;
        return 0;
    }
    int n = snprintf(out, outsz, "%s/%s", ld, item);
    return n > 0 && (size_t)n < outsz;
}

int comm_state_item_has_embedded_fc(const char* item)
{
    if (!item)
        return 0;
    const char* p = strchr(item, '/');
    p = strchr(p ? p + 1 : item, '$');
    if (!p || !p[1] || !p[2] || p[3] != '$')
        return 0;
    return isalpha((unsigned char)p[1]) && isalpha((unsigned char)p[2]);
}

int comm_state_dollar_ref_unsafe_with_fc(const char* objectReference)
{
    if (!objectReference || !strchr(objectReference, '$'))
        return 0;
    if (comm_state_item_has_embedded_fc(objectReference))
        return 0;
    /* LPHD1$Proxy$stVal + FC_ST → LPHD1$Proxy$stVal$ST */
    return strstr(objectReference, "$Proxy") != NULL || strchr(objectReference, '$') != NULL;
}

void comm_state_fcda_set_defaults(CommStateFcda* fcda)
{
    if (!fcda)
        return;
    memset(fcda, 0, sizeof(*fcda));
    copy_trunc(fcda->ln, sizeof(fcda->ln), "LPHD1");
    copy_trunc(fcda->doName, sizeof(fcda->doName), "Proxy");
    copy_trunc(fcda->daName, sizeof(fcda->daName), "stVal");
    copy_trunc(fcda->fc, sizeof(fcda->fc), "ST");
}

static int copy_attr(const char* line, const char* key, char* out, size_t n)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", key);
    const char* p = strstr(line, pat);
    if (!p) {
        out[0] = 0;
        return 0;
    }
    p += strlen(pat);
    const char* end = strchr(p, '"');
    if (!end) {
        out[0] = 0;
        return 0;
    }
    size_t len = (size_t)(end - p);
    if (len >= n)
        len = n - 1;
    memcpy(out, p, len);
    out[len] = 0;
    return 1;
}

static void fill_from_fcda_line(const char* line, CommStateFcda* fcda)
{
    char ldInst[COMM_STATE_LD_LEN];
    char prefix[40];
    char lnClass[40];
    char lnInst[16];
    char doName[80];
    char daName[40];
    char fc[8];
    ldInst[0] = prefix[0] = lnClass[0] = lnInst[0] = doName[0] = daName[0] = fc[0] = 0;
    copy_attr(line, "ldInst", ldInst, sizeof(ldInst));
    copy_attr(line, "prefix", prefix, sizeof(prefix));
    copy_attr(line, "lnClass", lnClass, sizeof(lnClass));
    copy_attr(line, "lnInst", lnInst, sizeof(lnInst));
    copy_attr(line, "doName", doName, sizeof(doName));
    copy_attr(line, "daName", daName, sizeof(daName));
    copy_attr(line, "fc", fc, sizeof(fc));
    comm_state_fcda_set_defaults(fcda);
    if (ldInst[0]) {
        if (strcmp(ldInst, "LD0") == 0)
            copy_trunc(fcda->ld, sizeof(fcda->ld), "SubStationLD0");
        else
            copy_trunc(fcda->ld, sizeof(fcda->ld), ldInst);
    }
    if (lnClass[0]) {
        if (!lnInst[0])
            copy_trunc(lnInst, sizeof(lnInst), "1");
        snprintf(fcda->ln, sizeof(fcda->ln), "%s%s%s", prefix, lnClass, lnInst);
    }
    if (doName[0])
        copy_trunc(fcda->doName, sizeof(fcda->doName), doName);
    if (daName[0])
        copy_trunc(fcda->daName, sizeof(fcda->daName), daName);
    else if (strchr(fcda->doName, '.') == NULL)
        copy_trunc(fcda->daName, sizeof(fcda->daName), "stVal");
    if (fc[0])
        copy_trunc(fcda->fc, sizeof(fcda->fc), fc);
    if (prefix[0])
        copy_trunc(fcda->ied, sizeof(fcda->ied), prefix);
    else
        comm_state_ied_from_ld(fcda->ld, fcda->ied, sizeof(fcda->ied));
}

int comm_state_fcda_is_proxy(const CommStateFcda* fcda)
{
    if (!fcda || !fcda->doName[0])
        return 0;
    return strcmp(fcda->doName, "Proxy") == 0;
}

int comm_state_fcda_is_comstatus(const CommStateFcda* fcda)
{
    if (!fcda || !fcda->doName[0])
        return 0;
    return strcmp(fcda->doName, "ComStatus") == 0;
}

static int already_has_ld(const CommStateFcda* out, int n, const char* ld)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(out[i].ld, ld) == 0)
            return 1;
    }
    return 0;
}

static int already_has_ied(char names[][COMM_STATE_IED_LEN], int n, const char* name)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(names[i], name) == 0)
            return 1;
    }
    return 0;
}

int comm_state_parse_smcd_fcdas(const char* path, CommStateFcda* out, int maxOut)
{
    if (!path || !out || maxOut <= 0)
        return 0;
    FILE* f = open_utf8(path);
    if (!f)
        return 0;

    char lineBuf[1024];
    char iedList[COMM_STATE_MAX_FCDA][COMM_STATE_IED_LEN];
    int nIed = 0;
    int inSub = 0;
    int inDs = 0;
    int n = 0;

    while (fgets(lineBuf, sizeof(lineBuf), f)) {
        const char* ip = lineBuf;
        while ((ip = strstr(ip, "<IED")) != NULL) {
            char tagName[COMM_STATE_IED_LEN];
            tagName[0] = 0;
            copy_attr(ip, "name", tagName, sizeof(tagName));
            ip += 4;
            if (!tagName[0])
                continue;
            if (strcmp(tagName, "SubStation") == 0) {
                inSub = 1;
                inDs = 0;
                continue;
            }
            if (inSub)
                inSub = 0;
            if (nIed < COMM_STATE_MAX_FCDA && !already_has_ied(iedList, nIed, tagName))
                copy_trunc(iedList[nIed++], COMM_STATE_IED_LEN, tagName);
        }
        if (!inSub)
            continue;
        if (strstr(lineBuf, "name=\"dsCommState\"")) {
            inDs = 1;
            continue;
        }
        if (inDs && strstr(lineBuf, "</DataSet>")) {
            inDs = 0;
            inSub = 0;
            continue;
        }
        if (inDs && strstr(lineBuf, "<FCDA") && n < maxOut) {
            fill_from_fcda_line(lineBuf, &out[n]);
            if (out[n].ld[0] && out[n].ln[0] && out[n].doName[0])
                n++;
        }
        if (inSub && strstr(lineBuf, "</IED>")) {
            inSub = 0;
            inDs = 0;
        }
    }
    fclose(f);

    if (n > 0)
        return n;

    /* No SubStation dsCommState: synthesize LD0/LPHD1.Proxy from IED names. */
    for (int i = 0; i < nIed && n < maxOut; i++) {
        comm_state_fcda_set_defaults(&out[n]);
        snprintf(out[n].ld, sizeof(out[n].ld), "%sLD0", iedList[i]);
        copy_trunc(out[n].ied, sizeof(out[n].ied), iedList[i]);
        if (!already_has_ld(out, n, out[n].ld))
            n++;
    }
    return n;
}
