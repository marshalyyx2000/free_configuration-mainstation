package com.libiec61850.tools;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** 通信工况点引用 LPHD1.Proxy，折叠模型常缺该 LN；向 cfg 注入最小 LPHD1。 */
public final class CfgLphdAugmentor {

    private static final Pattern DE_PROXY =
            Pattern.compile("DE\\(([^/]+)/LPHD1\\$ST\\$Proxy\\);");

    private static final String LPHD_MIN =
            "LN(LPHD1){\n"
                    + "DO(Proxy 0){\n"
                    + "DA(stVal 0 12 0 1 0);\n"
                    + "DA(q 0 23 0 2 0);\n"
                    + "DA(t 0 22 0 0 0);\n"
                    + "}\n"
                    + "}\n";

    private CfgLphdAugmentor() {}

    public static void ensureCommStateProxy(File cfg) throws IOException {
        if (!cfg.isFile() || cfg.length() < 100) {
            return;
        }
        String text = new String(Files.readAllBytes(cfg.toPath()), StandardCharsets.UTF_8);
        Set<String> ldNames = new LinkedHashSet<>();
        Matcher m = DE_PROXY.matcher(text);
        while (m.find()) {
            ldNames.add(m.group(1));
        }
        if (ldNames.isEmpty()) {
            return;
        }
        List<int[]> inserts = new ArrayList<>();
        for (String ld : ldNames) {
            String marker = "LD(" + ld + "){";
            int idx = text.indexOf(marker);
            if (idx < 0) {
                continue;
            }
            int insertAt = idx + marker.length();
            /* LPHD1 可能位于 LLN0 之后（完整装置 LD0），不能只看开头 400 字符，
               否则会再插入一个最小 LPHD1，模型加载时报重复 LN。 */
            int nextLd = text.indexOf("\nLD(", insertAt);
            if (nextLd < 0) {
                nextLd = text.length();
            }
            String block = text.substring(insertAt, nextLd);
            if (block.contains("LN(LPHD1)")) {
                continue;
            }
            inserts.add(new int[] {insertAt, insertAt});
        }
        if (inserts.isEmpty()) {
            return;
        }
        inserts.sort(Comparator.comparingInt((int[] a) -> a[0]).reversed());
        StringBuilder sb = new StringBuilder(text.length() + inserts.size() * LPHD_MIN.length());
        int cursor = text.length();
        for (int[] ins : inserts) {
            int at = ins[0];
            sb.insert(0, text.substring(at, cursor));
            sb.insert(0, LPHD_MIN);
            cursor = at;
        }
        sb.insert(0, text.substring(0, cursor));
        Files.write(cfg.toPath(), sb.toString().getBytes(StandardCharsets.UTF_8));
    }
}
