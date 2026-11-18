#ifndef RPT_ID_MATCH_H
#define RPT_ID_MATCH_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Server rptID "NULL" is rewritten by the configuration-free substation to "<IED>NULL".
 * libiec61850 otherwise matches only the exact registered string, so a handler
 * installed on the RCB object reference never sees the GI report. */

static inline bool rpt_id_is_null_token(const char* s)
{
    return s && (strcmp(s, "NULL") == 0 || strcmp(s, "null") == 0);
}

static inline void rpt_id_ied_from_domain(const char* domain, char* ied, size_t iedsz)
{
    size_t n;
    if (!ied || iedsz == 0)
        return;
    ied[0] = 0;
    if (!domain)
        return;
    n = strlen(domain);
    while (n > 0 && isdigit((unsigned char)domain[n - 1]))
        n--;
    if (n >= 2 && domain[n - 2] == 'L' && domain[n - 1] == 'D')
        n -= 2;
    if (n >= iedsz)
        n = iedsz - 1;
    if (n > 0)
        memcpy(ied, domain, n);
    ied[n] = 0;
}

static inline bool rpt_id_matches(const char* incoming, const char* registered, const char* rcbRef)
{
    char norm[160];
    char domain[80];
    char ied[80];
    char expect[96];
    size_t i;
    size_t d;
    const char* cut;

    if (!incoming || incoming[0] == 0)
        return false;
    if (registered && registered[0] && strcmp(incoming, registered) == 0)
        return true;
    if (!rcbRef || rcbRef[0] == 0)
        return false;
    if (strcmp(incoming, rcbRef) == 0)
        return true;

    for (i = 0; rcbRef[i] && i + 1 < sizeof(norm); i++)
        norm[i] = (rcbRef[i] == '.') ? '$' : rcbRef[i];
    norm[i] = 0;
    if (strcmp(incoming, norm) == 0)
        return true;

    if (!(rpt_id_is_null_token(incoming)
            || (strlen(incoming) >= 4 && strcmp(incoming + strlen(incoming) - 4, "NULL") == 0)))
        return false;

    cut = strchr(rcbRef, '/');
    if (!cut)
        cut = strchr(rcbRef, '.');
    d = cut ? (size_t)(cut - rcbRef) : strlen(rcbRef);
    if (d >= sizeof(domain))
        d = sizeof(domain) - 1;
    memcpy(domain, rcbRef, d);
    domain[d] = 0;
    rpt_id_ied_from_domain(domain, ied, sizeof(ied));
    if (ied[0] == 0)
        return false;
    if (rpt_id_is_null_token(incoming))
        return true;
    snprintf(expect, sizeof(expect), "%sNULL", ied);
    return strcmp(incoming, expect) == 0;
}

/* reason == notIncluded (0) drops the element unless the value changed,
 * or this report is the answer to a general interrogation. */
static inline int report_item_keep(int reason, int notIncluded, int valueChanged, int giPending)
{
    if (reason != notIncluded)
        return 1;
    if (giPending)
        return 1;
    return valueChanged ? 1 : 0;
}

#endif
