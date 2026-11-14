package com.libiec61850.tools;

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
 * 从折叠 SubStation CID 中流式抽取各装置 LDevice：保留 LN0 数据集/RCB，以及 DataSet FCDA
 * 引用到的全部 LN（自关闭标签，无 DOI 初值树），避免整文件 DOM 与 76MB 一次性入堆。
 */
public final class FullLnLDevicePruner {

    private static final Pattern LD_OPEN =
            Pattern.compile("<LDevice\\b[^>]*\\binst=\"([^\"]+)\"[^>]*>", Pattern.CASE_INSENSITIVE);
    private static final Pattern LN_LPHD =
            Pattern.compile("<LN\\b[^>]*\\blnClass=\"LPHD\"[^>]*>(?:[\\s\\S]*?</LN>|\\s*/>)",
                    Pattern.CASE_INSENSITIVE);
    private static final Pattern LN0_BLOCK =
            Pattern.compile("<LN0\\b[^>]*>[\\s\\S]*?</LN0>", Pattern.CASE_INSENSITIVE);
    private static final Pattern DS_TAG =
            Pattern.compile("<DataSet\\b[^>]*>[\\s\\S]*?</DataSet>", Pattern.CASE_INSENSITIVE);
    private static final Pattern RC_TAG =
            Pattern.compile("<ReportControl\\b[^>]*(?:/>|>[\\s\\S]*?</ReportControl>)", Pattern.CASE_INSENSITIVE);
    private static final Pattern FCDA_TAG = Pattern.compile("<FCDA\\b[^>]*>", Pattern.CASE_INSENSITIVE);
    private static final Pattern ATTR = Pattern.compile("([A-Za-z_][A-Za-z0-9_]*)=\"([^\"]*)\"");

    private static final int SCAN_BUF = 1 << 20;
    private static final int MAX_LD_BYTES = 32 * 1024 * 1024;

    private FullLnLDevicePruner() {}

    public static byte[] augmentMiniWithFullLnDevices(File cid, byte[] miniXml, String dsCommStateXml)
            throws IOException {
        WantLdFilter filter = wantLogicalDevices(dsCommStateXml);
        if (filter.fullLdInst.isEmpty()) {
            return miniXml;
        }
        long[] bounds = SclGenconfigSlice.findSubStationIedBounds(cid);
        if (bounds != null && bounds[1] - bounds[0] > 8L * 1024 * 1024) {
            /* 装置 fold 后 supervision.CID：单 IED ~70MB */
        } else {
            bounds = SclGenconfigSlice.findAllIedsBeforeTemplates(cid);
        }
        if (bounds == null) {
            return miniXml;
        }
        List<String> ldBlocks = streamExtractLDevices(cid, bounds[0], bounds[1], filter);
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

    private static final Pattern IED_NAME = Pattern.compile("<IED\\b[^>]*\\bname=\"([^\"]+)\"");

    static final class WantLdFilter {
        final Set<String> fullLdInst = new LinkedHashSet<>();
        final Set<String> iedNames = new LinkedHashSet<>();
    }

    private static WantLdFilter wantLogicalDevices(String dsCommStateXml) {
        WantLdFilter f = new WantLdFilter();
        Matcher m = Pattern.compile("ldInst=\"([^\"]+)\"").matcher(dsCommStateXml);
        while (m.find()) {
            String w = m.group(1);
            if (!w.endsWith("LD0") || w.length() <= 3) {
                continue;
            }
            String prefix = w.substring(0, w.length() - 3);
            f.iedNames.add(prefix);
            f.fullLdInst.add(w);
            f.fullLdInst.add(prefix + "PROT");
            f.fullLdInst.add(prefix + "CTRL");
            f.fullLdInst.add(prefix + "MEAS");
        }
        return f;
    }

    private static boolean wantDevice(WantLdFilter filter, String currentIed, String ldInst) {
        if (filter.fullLdInst.contains(ldInst)) {
            return true;
        }
        if (currentIed == null || currentIed.isEmpty()) {
            return false;
        }
        if (filter.fullLdInst.contains(currentIed + ldInst)) {
            return true;
        }
        return filter.iedNames.contains(currentIed) && "LD0".equals(ldInst);
    }

    static List<String> streamExtractLDevices(File cid, long rangeStart, long rangeEnd, WantLdFilter filter)
            throws IOException {
        List<String> out = new ArrayList<>();
        byte[] needle = "<LDevice".getBytes(StandardCharsets.US_ASCII);
        long pos = rangeStart;
        try (RandomAccessFile raf = new RandomAccessFile(cid, "r")) {
            while (pos < rangeEnd - 8) {
                long ldHit = findBytes(raf, needle, pos, rangeEnd);
                if (ldHit < 0) {
                    break;
                }
                long hit = ldHit;
                String currentIed = iedNameBefore(raf, hit, rangeStart);
                long headerEnd = readUntilByte(raf, hit, (byte) '>', rangeEnd);
                if (headerEnd < 0) {
                    pos = hit + 1;
                    continue;
                }
                String header = readUtf8Range(raf, hit, headerEnd + 1);
                Matcher open = LD_OPEN.matcher(header);
                if (!open.find()) {
                    pos = hit + 1;
                    continue;
                }
                String inst = open.group(1);
                long ldEnd = findLDeviceEndAbsolute(raf, headerEnd + 1, rangeEnd);
                if (ldEnd < 0) {
                    pos = hit + 1;
                    continue;
                }
                if (wantDevice(filter, currentIed, inst)) {
                    String body = readUtf8Range(raf, hit, ldEnd);
                    String outInst = inst;
                    if ("LD0".equals(inst) && currentIed != null && !currentIed.equals("SubStation")) {
                        outInst = currentIed + "LD0";
                        body = body.replaceFirst("inst=\"LD0\"", "inst=\"" + outInst + "\"");
                    }
                    String pruned = pruneLDeviceFullLn(body, outInst);
                    if (pruned != null) {
                        out.add(pruned);
                    }
                }
                pos = ldEnd;
            }
        }
        return out;
    }

  /** 从 LDevice 标签向前最多 512KB 取最近的 &lt;IED name=&quot;…&quot;&gt;。 */
    private static String iedNameBefore(RandomAccessFile raf, long ldHit, long rangeStart) throws IOException {
        long scanFrom = Math.max(rangeStart, ldHit - (512L * 1024));
        String chunk = readUtf8Range(raf, scanFrom, ldHit);
        int last = chunk.lastIndexOf("<IED");
        if (last < 0) {
            return null;
        }
        int gt = chunk.indexOf('>', last);
        if (gt < 0) {
            return null;
        }
        Matcher im = IED_NAME.matcher(chunk.substring(last, gt + 1));
        return im.find() ? im.group(1) : null;
    }

    private static long findBytes(RandomAccessFile raf, byte[] needle, long start, long limit) throws IOException {
        int patLen = needle.length;
        long pos = start;
        byte[] buf = new byte[SCAN_BUF];
        byte[] carry = new byte[patLen - 1];
        int carryLen = 0;
        while (pos < limit) {
            raf.seek(pos);
            int n = raf.read(buf, 0, (int) Math.min(buf.length, limit - pos));
            if (n <= 0) {
                break;
            }
            int hayLen = carryLen + n;
            byte[] hay = new byte[hayLen];
            System.arraycopy(carry, 0, hay, 0, carryLen);
            System.arraycopy(buf, 0, hay, carryLen, n);
            for (int i = 0; i <= hayLen - patLen; i++) {
                boolean ok = true;
                for (int j = 0; j < patLen; j++) {
                    if (hay[i + j] != needle[j]) {
                        ok = false;
                        break;
                    }
                }
                if (ok) {
                    return pos - carryLen + i;
                }
            }
            carryLen = Math.min(patLen - 1, hayLen);
            System.arraycopy(hay, hayLen - carryLen, carry, 0, carryLen);
            pos += n;
        }
        return -1;
    }

    private static long readUntilByte(RandomAccessFile raf, long start, byte target, long limit) throws IOException {
        raf.seek(start);
        long p = start;
        while (p < limit) {
            int b = raf.read();
            if (b < 0) {
                return -1;
            }
            if ((byte) b == target) {
                return p;
            }
            p++;
        }
        return -1;
    }

    private static long findLDeviceEndAbsolute(RandomAccessFile raf, long bodyStart, long rangeEnd)
            throws IOException {
        byte[] open = "<LDevice".getBytes(StandardCharsets.US_ASCII);
        byte[] close = "</LDevice>".getBytes(StandardCharsets.US_ASCII);
        int depth = 1;
        long pos = bodyStart;
        while (pos < rangeEnd && depth > 0) {
            long nextOpen = findBytes(raf, open, pos, rangeEnd);
            long nextClose = findBytes(raf, close, pos, rangeEnd);
            if (nextClose < 0) {
                return -1;
            }
            if (nextOpen >= 0 && nextOpen < nextClose) {
                depth++;
                pos = nextOpen + open.length;
                continue;
            }
            depth--;
            if (depth == 0) {
                return nextClose + close.length;
            }
            pos = nextClose + close.length;
        }
        return -1;
    }

    private static String readUtf8Range(RandomAccessFile raf, long start, long end) throws IOException {
        int len = (int) Math.min(MAX_LD_BYTES, end - start);
        if (len <= 0) {
            return "";
        }
        byte[] buf = new byte[len];
        raf.seek(start);
        raf.readFully(buf);
        return new String(buf, StandardCharsets.UTF_8);
    }

    static String pruneLDeviceFullLn(String raw, String inst) {
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
        StringBuilder lns = new StringBuilder();
        Matcher lphd = LN_LPHD.matcher(raw);
        if (lphd.find()) {
            lns.append("\t\t\t\t\t").append(toSelfClosingLphd(lphd.group())).append("\n");
        }
        Set<String> wanted = referencedLnKeys(ln0Inner);
        appendReferencedLns(raw, wanted, lns);
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

    private static String toSelfClosingLphd(String lnBlock) {
        if (lnBlock.trim().endsWith("/>")) {
            return lnBlock.trim();
        }
        int gt = lnBlock.indexOf('>');
        if (gt < 0) {
            return lnBlock;
        }
        return lnBlock.substring(0, gt + 1).replaceFirst(">$", "/>");
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
            String key = attrValue(open, "prefix") + lnClass + attrValue(open, "inst");
            int close = raw.indexOf("</LN>", gt);
            int end = close >= 0 ? close + "</LN>".length() : gt + 1;
            if (wanted.contains(key) || "LPHD".equals(lnClass)) {
                String lnTag = open.endsWith("/>")
                        ? open
                        : open.substring(0, open.length() - 1) + "/>";
                out.append("\t\t\t\t\t").append(lnTag).append("\n");
            }
            pos = end;
        }
    }

}
