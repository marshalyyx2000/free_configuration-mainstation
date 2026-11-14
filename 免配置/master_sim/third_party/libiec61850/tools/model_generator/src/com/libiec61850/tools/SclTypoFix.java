package com.libiec61850.tools;

/**
 * 部分厂家 ICD 把逻辑设备实例 LD0（数字 0）写成 LDO（字母 O）。
 * convert2CID 再把 IED 名拼到 rptID 前面后，变成 P_xxxALDO/...，cfg 里 RC 第二列
 * 就指向不存在的域名。此处只改路径分隔形式 {@code LDO/}，避免误伤其它标识。
 */
public final class SclTypoFix {

    private SclTypoFix() {}

    public static String fixLdZeroLetterO(String s) {
        if (s == null || s.indexOf("LDO/") < 0) {
            return s;
        }
        return s.replace("LDO/", "LD0/");
    }

    public static void main(String[] args) {
        String[][] cases = {
            {"LDO/LLN0$BR$brcbDeviceState", "LD0/LLN0$BR$brcbDeviceState"},
            {"P_M5001ALDO/LLN0$BR$brcbDeviceState", "P_M5001ALD0/LLN0$BR$brcbDeviceState"},
            {"P_M5001ALD0/LLN0$BR$brcbRelayDin", "P_M5001ALD0/LLN0$BR$brcbRelayDin"},
            {"LD0/LLN0$BR$brcbAlarm", "LD0/LLN0$BR$brcbAlarm"},
            {null, null},
            {"", ""},
        };
        int fail = 0;
        for (String[] c : cases) {
            String got = fixLdZeroLetterO(c[0]);
            String exp = c[1];
            boolean ok = (got == null && exp == null) || (got != null && got.equals(exp));
            if (!ok) {
                System.err.println("FAIL in=" + c[0] + " got=" + got + " exp=" + exp);
                fail++;
            }
        }
        if (fail != 0) {
            System.exit(1);
        }
        System.out.println("SclTypoFix PASS " + cases.length);
    }
}
