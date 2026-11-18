#include "rpt_id_match.h"

#include <stdio.h>

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
    const char* rcb = "E1Q1SB20LD0/LLN0$BR$brcbWarning01";
    const char* other = "E1Q1SB21LD0/LLN0$BR$brcbWarning01";

    CHECK(rpt_id_matches(rcb, rcb, rcb));
    CHECK(rpt_id_matches(rcb, NULL, rcb));
    CHECK(rpt_id_matches("E1Q1SB20NULL", rcb, rcb));
    CHECK(rpt_id_matches("E1Q1SB20NULL", "E1Q1SB20NULL", rcb));
    CHECK(!rpt_id_matches("E1Q1SB21NULL", rcb, rcb));
    CHECK(rpt_id_matches("E1Q1SB21NULL", other, other));
    CHECK(rpt_id_matches("NULL", rcb, rcb));
    CHECK(rpt_id_matches("null", "something-else", rcb));
    CHECK(rpt_id_matches("brcbWarning", "brcbWarning", rcb));
    CHECK(!rpt_id_matches("brcbWarning", rcb, rcb));
    CHECK(rpt_id_matches(
        "simpleIOGenericIO/LLN0$RP$EventsRCB01",
        "simpleIOGenericIO/LLN0.RP.EventsRCB01",
        "simpleIOGenericIO/LLN0.RP.EventsRCB01"));
    CHECK(!rpt_id_matches("", rcb, rcb));
    CHECK(!rpt_id_matches(NULL, rcb, rcb));

    {
        char ied[32];
        rpt_id_ied_from_domain("E1Q1SB20LD0", ied, sizeof(ied));
        CHECK(strcmp(ied, "E1Q1SB20") == 0);
        rpt_id_ied_from_domain("PROTLD10", ied, sizeof(ied));
        CHECK(strcmp(ied, "PROT") == 0);
    }

    /* GI reason 16 is always kept. Reason 0 is kept on GI or when the value changed. */
    CHECK(report_item_keep(16, 0, 0, 0) == 1);
    CHECK(report_item_keep(1, 0, 0, 0) == 1);
    CHECK(report_item_keep(0, 0, 0, 0) == 0);
    CHECK(report_item_keep(0, 0, 1, 0) == 1);
    CHECK(report_item_keep(0, 0, 0, 1) == 1);
    CHECK(report_item_keep(8, 0, 0, 0) == 1);

    if (g_fail) {
        fprintf(stderr, "%d checks failed\n", g_fail);
        return 1;
    }
    printf("ok %s\n", "rpt_id_match");
    return 0;
}
