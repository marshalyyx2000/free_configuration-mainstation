package com.libiec61850.tools;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * 从大体积折叠 SubStation CID/SMCD 中抽取各装置 LDevice（仅 LPHD + LN0 数据集/报告），
 * 拼入 fast slice 生成的迷你 SCL，供 DynamicModelGenerator 生成代理侧 LPHD/RCB 模型。
 */
public final class CollapsedLDevicePruner {

    private static final Pattern LD_OPEN =
            Pattern.compile("<LDevice\\b[^>]*\\binst=\"([^\"]+)\"[^>]*>", Pattern.CASE_INSENSITIVE);
    private static final Pattern FCDA_LD =
            Pattern.compile("ldInst=\"([^\"]+)\"", Pattern.CASE_INSENSITIVE);
    private static final Pattern LN_LPHD =
            Pattern.compile("<LN\\b[^>]*\\blnClass=\"LPHD\"[^>]*>[\\s\\S]*?</LN>", Pattern.CASE_INSENSITIVE);
    private static final Pattern FCDA_TAG =
            Pattern.compile("<FCDA\\b[^>]*>", Pattern.CASE_INSENSITIVE);
    private static final Pattern ATTR =
            Pattern.compile("([A-Za-z_][A-Za-z0-9_]*)=\"([^\"]*)\"");
    private static final Pattern LN0_BLOCK =
            Pattern.compile("<LN0\\b[^>]*>[\\s\\S]*?</LN0>", Pattern.CASE_INSENSITIVE);
    private static final Pattern DS_TAG =
            Pattern.compile("<DataSet\\b[^>]*>[\\s\\S]*?</DataSet>", Pattern.CASE_INSENSITIVE);
    private static final Pattern RC_TAG =
            Pattern.compile("<ReportControl\\b[^>]*(?:/>|>[\\s\\S]*?</ReportControl>)", Pattern.CASE_INSENSITIVE);

    private CollapsedLDevicePruner() {}

    public static byte[] augmentMiniWithCollapsedDevices(File cid, byte[] miniXml, String dsCommStateXml)
            throws IOException {
        Set<String> wantLd = new LinkedHashSet<>();
        Matcher m = FCDA_LD.matcher(dsCommStateXml);
        while (m.find()) {
            wantLd.add(m.group(1));
        }
        Set<String> forExtract = new LinkedHashSet<>();
        for (String w : wantLd) {
            if (!w.endsWith("LD0")) {
                continue;
            }
            String prefix = w.substring(0, w.length() - 3);
            /* LD0 本身承载各装置 LLN0 的 dsAlarm/dsWarning/dsRelayDin 等数据集与
               brcbAlarm/... 报告控制块，主站订阅与召唤都指向 LD0；只取 PROT/CTRL/MEAS
               会生成没有 LD0 域、没有这些数据集/RCB 的残缺模型。 */
            forExtract.add(w);
            forExtract.add(prefix + "PROT");
            forExtract.add(prefix + "CTRL");
            forExtract.add(prefix + "MEAS");
        }
        wantLd = forExtract;
        long[] bounds = SclGenconfigSlice.findSubStationIedBounds(cid);
        if (bounds == null) {
            return miniXml;
        }
        String collapsed = readRangeUtf8(cid, bounds[0], bounds[1]);
        List<String> ldBlocks = extractPrunedLDevices(collapsed, wantLd);
        if (ldBlocks.isEmpty()) {
            return miniXml;
        }
        String mini = new String(miniXml, StandardCharsets.UTF_8);
        int insertAt = mini.lastIndexOf("</Server>");
        if (insertAt < 0) {
            insertAt = mini.lastIndexOf("</AccessPoint>");
        }
        if (insertAt < 0) {
            return miniXml;
        }
        StringBuilder extra = new StringBuilder();
        for (String ld : ldBlocks) {
            extra.append("\t\t\t\t").append(ld).append("\n");
        }
        String merged = mini.substring(0, insertAt) + extra + mini.substring(insertAt);
        return merged.getBytes(StandardCharsets.UTF_8);
    }

    private static List<String> extractPrunedLDevices(String collapsed, Set<String> wantLdInst) {
        List<String> out = new ArrayList<>();
        int pos = 0;
        while (pos < collapsed.length()) {
            Matcher open = LD_OPEN.matcher(collapsed);
            if (!open.find(pos)) {
                break;
            }
            String inst = open.group(1);
            int start = open.start();
            int end = findLDeviceClose(collapsed, open.end());
            if (end < 0) {
                break;
            }
            pos = end + "</LDevice>".length();
            if (!wantLdInst.contains(inst)) {
                continue;
            }
            String body = collapsed.substring(start, end + "</LDevice>".length());
            String pruned = pruneLDevice(body, inst);
            if (pruned != null) {
                out.add(pruned);
            }
        }
        return out;
    }

    private static String pruneLDevice(String raw, String inst) {
        Matcher ln0m = LN0_BLOCK.matcher(raw);
        if (!ln0m.find()) {
            return null;
        }
        String ln0Inner = ln0m.group();
        StringBuilder ln0Kids = new StringBuilder();
        Matcher ds = DS_TAG.matcher(ln0Inner);
        while (ds.find()) {
            ln0Kids.append("\t\t\t\t\t\t").append(ds.group()).append("\n");
        }
        Matcher rc = RC_TAG.matcher(ln0Inner);
        while (rc.find()) {
            ln0Kids.append("\t\t\t\t\t\t").append(rc.group()).append("\n");
        }
        /* 代理侧只保留 LPHD + LN0（数据集/报告控制块），保持模型轻量、启动快。
           FCDA 指向的 DO/DA 不在本地模型中，报告值会由主站侧按数据引用展示；
           完整 LN 展开会让 159 台装置的模型膨胀到 60MB+、启动数分钟。 */
        StringBuilder lns = new StringBuilder();
        Matcher lphd = LN_LPHD.matcher(raw);
        if (lphd.find()) {
            lns.append("\t\t\t\t\t").append(lphd.group()).append("\n");
        }
        if (ln0Kids.length() == 0 && lns.length() == 0) {
            return null;
        }
        String ldOpen = raw.substring(0, raw.indexOf('>') + 1);
        String ln0Open = ln0Inner.substring(0, ln0Inner.indexOf('>') + 1);
        return ldOpen
                + "\n"
                + "\t\t\t\t\t"
                + ln0Open
                + "\n"
                + ln0Kids
                + "\t\t\t\t\t</LN0>\n"
                + lns
                + "\t\t\t\t</LDevice>";
    }

    private static String attrValue(String tag, String name) {
        Matcher m = ATTR.matcher(tag);
        while (m.find()) {
            if (m.group(1).equals(name)) {
                return m.group(2);
            }
        }
        return "";
    }

    private static Set<String> referencedLnKeys(String ln0Inner) {
        Set<String> keys = new LinkedHashSet<>();
        Matcher m = FCDA_TAG.matcher(ln0Inner);
        while (m.find()) {
            String tag = m.group();
            String key = attrValue(tag, "prefix") + attrValue(tag, "lnClass") + attrValue(tag, "lnInst");
            if (key.length() > 0) {
                keys.add(key);
            }
        }
        return keys;
    }

    private static void appendReferencedLns(String raw, Set<String> wanted, StringBuilder out) {
        int pos = 0;
        while (pos < raw.length()) {
            int start = raw.indexOf("<LN", pos);
            if (start < 0) {
                break;
            }
            char c = start + 3 < raw.length() ? raw.charAt(start + 3) : ' ';
            if (c == '0') {
                pos = start + 4;
                continue;
            }
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '>') {
                pos = start + 3;
                continue;
            }
            int gt = raw.indexOf('>', start);
            if (gt < 0) {
                break;
            }
            String open = raw.substring(start, gt + 1);
            String lnClass = attrValue(open, "lnClass");
            /* LN 元素的实例属性是 inst（FCDA 才是 lnInst）。 */
            String key = attrValue(open, "prefix") + lnClass + attrValue(open, "inst");
            int close = raw.indexOf("</LN>", gt);
            if (close < 0) {
                break;
            }
            int end = close + "</LN>".length();
            if (wanted.contains(key) || "LPHD".equals(lnClass)) {
                /* 只保留 LN 声明（lnType 会在 DataTypeTemplates 里展开成 DO/DA）。
                   原 LN 下的 DOI/DAI 初值树可达数十 MB，且 DynamicModelGenerator
                   会逐点解析，导致 50MB mini SCL 生成耗时数分钟；代理模型的值
                   来自下联装置报告，不需要这些初值。 */
                String lnTag = open.endsWith("/>")
                        ? open
                        : open.substring(0, open.length() - 1) + "/>";
                out.append("\t\t\t\t\t").append(lnTag).append("\n");
            }
            pos = end;
        }
    }

    private static int findLDeviceClose(String s, int from) {
        int depth = 1;
        int pos = from;
        Pattern open = Pattern.compile("<LDevice\\b", Pattern.CASE_INSENSITIVE);
        Pattern close = Pattern.compile("</LDevice>", Pattern.CASE_INSENSITIVE);
        while (pos < s.length() && depth > 0) {
            Matcher co = close.matcher(s);
            Matcher op = open.matcher(s);
            int nextClose = co.find(pos) ? co.start() : -1;
            int nextOpen = op.find(from) ? op.start() : -1;
            if (nextClose < 0) {
                return -1;
            }
            if (nextOpen >= 0 && nextOpen < nextClose) {
                depth++;
                from = nextOpen + 8;
                pos = from;
                continue;
            }
            depth--;
            if (depth == 0) {
                return nextClose;
            }
            pos = nextClose + "</LDevice>".length();
            from = pos;
        }
        return -1;
    }

    private static String readRangeUtf8(File f, long start, long end) throws IOException {
        int len = (int) Math.min(Integer.MAX_VALUE, end - start);
        byte[] buf = new byte[len];
        try (RandomAccessFile raf = new RandomAccessFile(f, "r")) {
            raf.seek(start);
            raf.readFully(buf);
        }
        return new String(buf, StandardCharsets.UTF_8);
    }
}
