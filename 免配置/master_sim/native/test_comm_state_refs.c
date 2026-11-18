#include "comm_state_refs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

static int g_fail = 0;

static void expect(int cond, const char* name)
{
    if (cond) {
        printf("PASS  %s\n", name);
        return;
    }
    printf("FAIL  %s\n", name);
    g_fail++;
}

static void expect_eq(const char* got, const char* want, const char* name)
{
    if (got && want && strcmp(got, want) == 0) {
        printf("PASS  %s\n", name);
        return;
    }
    printf("FAIL  %s got=[%s] want=[%s]\n", name, got ? got : "(null)", want ? want : "(null)");
    g_fail++;
}

static void write_fixture(char* path, size_t n)
{
#ifdef _WIN32
    snprintf(path, n, "%s\\smcd_fcda_fixture.xml", getenv("TEMP") ? getenv("TEMP") : ".");
#else
    snprintf(path, n, "/tmp/smcd_fcda_fixture.xml");
#endif
    FILE* f = fopen(path, "wb");
    if (!f) {
        path[0] = 0;
        return;
    }
    fputs("<?xml version=\"1.0\"?>\n<SCL>\n", f);
    fputs("  <IED name=\"SubStation\" desc=\"substation\">\n", f);
    fputs("    <AccessPoint name=\"S1\"><Server><LDevice inst=\"LD0\">\n", f);
    fputs("      <LN0 lnClass=\"LLN0\" inst=\"\">\n", f);
    fputs("        <DataSet name=\"dsCommState\">\n", f);
    fputs("          <FCDA doName=\"Proxy\" fc=\"ST\" ldInst=\"E1Q1SB20LD0\" lnClass=\"LPHD\" lnInst=\"1\"/>\n", f);
    fputs("          <FCDA doName=\"Proxy\" fc=\"ST\" ldInst=\"E1Q1SB31LD0\" lnClass=\"LPHD\" lnInst=\"1\"/>\n", f);
    fputs("          <FCDA doName=\"Proxy\" fc=\"ST\" ldInst=\"E1Q1SB29LD0\" lnClass=\"LPHD\" lnInst=\"1\"/>\n", f);
    fputs("        </DataSet>\n", f);
    fputs("      </LN0></LDevice></Server></AccessPoint>\n", f);
    fputs("  </IED>\n", f);
    fputs("  <IED name=\"E1Q1SB20\" desc=\"meas1\"></IED>\n", f);
    fputs("  <IED name=\"E1Q1SB31\" desc=\"busbar-prot\"></IED>\n", f);
    fputs("</SCL>\n", f);
    fclose(f);
}

int main(void)
{
    char ied[80];
    comm_state_ied_from_ld("E1Q1SB31LD0", ied, sizeof(ied));
    expect_eq(ied, "E1Q1SB31", "ied_from_ld LD0");
    comm_state_ied_from_ld("E1Q1SB31PROT", ied, sizeof(ied));
    expect_eq(ied, "E1Q1SB31", "ied_from_ld PROT");
    comm_state_ied_from_ld("E1Q1SB20MEAS", ied, sizeof(ied));
    expect_eq(ied, "E1Q1SB20", "ied_from_ld MEAS");
    comm_state_ied_from_ld("E1Q1SB20CTRL", ied, sizeof(ied));
    expect_eq(ied, "E1Q1SB20", "ied_from_ld CTRL");

    char prot[80];
    expect(comm_state_prot_ld_from_ld0("E1Q1SB31LD0", prot, sizeof(prot)), "prot_ld from LD0");
    expect_eq(prot, "E1Q1SB31PROT", "prot_ld value");
    expect(!comm_state_prot_ld_from_ld0("E1Q1SB31PROT", prot, sizeof(prot)), "prot_ld only from LD0");

    char obj[384];
    comm_state_build_object_ref(obj, sizeof(obj), "E1Q1SB31LD0", "LPHD1", "Proxy", "stVal");
    expect_eq(obj, "E1Q1SB31LD0/LPHD1.Proxy.stVal", "dotted object ref");
    expect(!strchr(obj, '$'), "dotted ref has no dollar");

    char item[192];
    comm_state_build_mms_item(item, sizeof(item), "LPHD1", "ST", "Proxy", "stVal");
    expect_eq(item, "LPHD1$ST$Proxy$stVal", "mms item inserts FC after LN");
    expect(strstr(item, "$ST$") != NULL, "mms item has $ST$");
    expect(!strstr(item, "stVal$ST"), "mms item must not append $ST at leaf");

    char mmsObj[384];
    comm_state_build_mms_object_ref(mmsObj, sizeof(mmsObj), "E1Q1SB31LD0", "LPHD1", "ST", "Proxy", "stVal");
    expect_eq(mmsObj, "E1Q1SB31LD0/LPHD1$ST$Proxy$stVal", "full mms object");

    expect(comm_state_item_has_embedded_fc("E1Q1SB31LD0/LPHD1$ST$Proxy$stVal"), "embedded FC detected");
    expect(!comm_state_item_has_embedded_fc("E1Q1SB31LD0/LPHD1$Proxy$stVal"), "missing FC not embedded");
    expect(comm_state_dollar_ref_unsafe_with_fc("E1Q1SB20LD0/LPHD1$Proxy$stVal"),
        "dollar without FC is unsafe with FC_ST");
    expect(!comm_state_dollar_ref_unsafe_with_fc("E1Q1SB20LD0/LPHD1.Proxy.stVal"),
        "dotted ref is safe");
    expect(!comm_state_dollar_ref_unsafe_with_fc("E1Q1SB20LD0/LPHD1$ST$Proxy$stVal"),
        "already-mapped mms ref is safe with FC_NONE");

    /* Wireshark failure form: FC appended after leaf */
    expect(strstr("LPHD1$Proxy$stVal$ST", "stVal$ST") != NULL, "document wireshark failure form");

    char fixture[512];
    write_fixture(fixture, sizeof(fixture));
    expect(fixture[0] != 0, "write fixture");
    CommStateFcda fcdas[16];
    int n = comm_state_parse_smcd_fcdas(fixture, fcdas, 16);
    expect(n == 3, "parse SubStation dsCommState count");
    if (n >= 2) {
        expect_eq(fcdas[0].ied, "E1Q1SB20", "fcda0 ied meas");
        expect_eq(fcdas[1].ied, "E1Q1SB31", "fcda1 ied prot");
        expect_eq(fcdas[1].ld, "E1Q1SB31LD0", "fcda1 ld");
        expect_eq(fcdas[1].ln, "LPHD1", "fcda1 ln");
        expect_eq(fcdas[1].doName, "Proxy", "fcda1 do");
        expect_eq(fcdas[1].daName, "stVal", "fcda1 da");
        expect_eq(fcdas[1].fc, "ST", "fcda1 fc");
        comm_state_build_object_ref(obj, sizeof(obj), fcdas[1].ld, fcdas[1].ln, fcdas[1].doName, fcdas[1].daName);
        expect_eq(obj, "E1Q1SB31LD0/LPHD1.Proxy.stVal", "protection object ref from FCDA");
        expect(!comm_state_dollar_ref_unsafe_with_fc(obj), "protection ref safe with FC_ST");
    }
    if (fixture[0])
        remove(fixture);

    /* 保信 dsCommState：SubStationLD0/{prefix}SDDI{inst}.ComStatus.stVal */
    {
#ifdef _WIN32
        snprintf(fixture, sizeof(fixture), "%s\\smcd_comstatus_fixture.xml", getenv("TEMP") ? getenv("TEMP") : ".");
#else
        snprintf(fixture, sizeof(fixture), "/tmp/smcd_comstatus_fixture.xml");
#endif
        FILE* f = fopen(fixture, "wb");
        expect(f != NULL, "write ComStatus fixture");
        if (f) {
            fputs("<?xml version=\"1.0\"?>\n<SCL>\n", f);
            fputs("  <IED name=\"SubStation\">\n", f);
            fputs("    <LDevice inst=\"LD0\"><LN0>\n", f);
            fputs("      <DataSet name=\"dsCommState\">\n", f);
            fputs("        <FCDA doName=\"ComStatus\" fc=\"ST\" ldInst=\"LD0\" lnClass=\"SDDI\" lnInst=\"1\" prefix=\"E1Q1SB20\"/>\n", f);
            fputs("        <FCDA doName=\"ComStatus\" fc=\"ST\" ldInst=\"LD0\" lnClass=\"SDDI\" lnInst=\"12\" prefix=\"E1Q1SB31\"/>\n", f);
            fputs("        <FCDA doName=\"ComStatus\" fc=\"ST\" ldInst=\"LD0\" lnClass=\"SDDI\" lnInst=\"30\" prefix=\"E1Q1SB49\"/>\n", f);
            fputs("      </DataSet>\n", f);
            fputs("    </LN0></LDevice>\n", f);
            fputs("  </IED>\n", f);
            fputs("  <IED name=\"E1Q1SB20\"></IED>\n", f);
            fputs("  <IED name=\"E1Q1SB31\"></IED>\n", f);
            fputs("</SCL>\n", f);
            fclose(f);
            n = comm_state_parse_smcd_fcdas(fixture, fcdas, 16);
            expect(n == 3, "parse SDDI ComStatus count");
            if (n >= 2) {
                expect_eq(fcdas[1].ied, "E1Q1SB31", "ComStatus ied from prefix");
                expect_eq(fcdas[1].ld, "SubStationLD0", "ComStatus ld is SubStationLD0");
                expect_eq(fcdas[1].ln, "E1Q1SB31SDDI12", "ComStatus ln prefix+SDDI+inst");
                expect_eq(fcdas[1].doName, "ComStatus", "ComStatus do");
                expect_eq(fcdas[1].daName, "stVal", "ComStatus da");
                expect_eq(fcdas[1].fc, "ST", "ComStatus fc");
                expect(comm_state_fcda_is_comstatus(&fcdas[1]), "is ComStatus");
                expect(!comm_state_fcda_is_proxy(&fcdas[1]), "ComStatus is not Proxy");
                comm_state_build_object_ref(obj, sizeof(obj), fcdas[1].ld, fcdas[1].ln, fcdas[1].doName, fcdas[1].daName);
                expect_eq(obj, "SubStationLD0/E1Q1SB31SDDI12.ComStatus.stVal", "ComStatus object ref");
                expect(!strchr(obj, '$'), "ComStatus dotted has no dollar");
                expect(!strstr(obj, "LPHD1$Proxy$stVal$ST"), "must not emit Proxy$stVal$ST");
                expect(!strstr(obj, "LPHD"), "ComStatus ref has no LPHD");
            }
            if (n >= 3) {
                expect_eq(fcdas[2].ied, "E1Q1SB49", "last ied prefix");
                expect_eq(fcdas[2].ln, "E1Q1SB49SDDI30", "last ln");
            }
            remove(fixture);
        }
    }

    /* No SubStation IED: synthesize LD0 Proxy from IED names */
    {
#ifdef _WIN32
        snprintf(fixture, sizeof(fixture), "%s\\smcd_no_sub.xml", getenv("TEMP") ? getenv("TEMP") : ".");
#else
        snprintf(fixture, sizeof(fixture), "/tmp/smcd_no_sub.xml");
#endif
        FILE* f = fopen(fixture, "wb");
        if (f) {
            fputs("<SCL><IED name=\"E1Q1SB20\"></IED><IED name=\"E1Q1SB31\"></IED></SCL>\n", f);
            fclose(f);
            n = comm_state_parse_smcd_fcdas(fixture, fcdas, 16);
            expect(n == 2, "fallback without SubStation");
            if (n >= 2) {
                expect_eq(fcdas[1].ied, "E1Q1SB31", "fallback protection ied");
                expect_eq(fcdas[1].ld, "E1Q1SB31LD0", "fallback protection ld");
            }
            remove(fixture);
        }
    }

    printf("%s\n", g_fail ? "FAILED" : "ALL PASSED");
    return g_fail ? 1 : 0;
}
