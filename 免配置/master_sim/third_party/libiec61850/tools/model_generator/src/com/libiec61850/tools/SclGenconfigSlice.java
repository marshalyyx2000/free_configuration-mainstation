package com.libiec61850.tools;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** 流式裁剪 SCL/CID：保留头/Communication、SubStation IED、DataTypeTemplates。 */
public final class SclGenconfigSlice {

    private static final byte[] IED_OPEN = "<IED".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] IED_CLOSE = "</IED>".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] DTT_OPEN = "<DataTypeTemplates>".getBytes(StandardCharsets.US_ASCII);
    private static final byte[] SUBSTATION =
            "manufacturer=\"master_sim\" name=\"SubStation\"".getBytes(StandardCharsets.US_ASCII);
    private static final int IO_BUF = 1 << 20;
    private static final int TAIL_SEARCH = 8 * 1024 * 1024;
    private static final int BACKSCAN = 65536;
    private static final long BLOATED_IED = 4L * 1024 * 1024;
    private static final Pattern DS_COMM =
            Pattern.compile("<DataSet[^>]*name=\"dsCommState\"[^>]*>[\\s\\S]*?</DataSet>");
    private static final Pattern LN_TYPE = Pattern.compile("lnType=\"([^\"]+)\"");

    private SclGenconfigSlice() {}

    public static void slice(File src, File dst) throws IOException {
        long len = src.length();
        long dttPos = findPatternFromEnd(src, DTT_OPEN, TAIL_SEARCH);
        if (dttPos < 0) {
            throw new IOException("未找到 <DataTypeTemplates>");
        }
        try (RandomAccessFile raf = new RandomAccessFile(src, "r");
                OutputStream out = new BufferedOutputStream(new FileOutputStream(dst), IO_BUF)) {
            long firstIed = findPattern(raf, IED_OPEN, 0, dttPos);
            if (firstIed < 0) {
                throw new IOException("未找到 <IED");
            }
            long subName = findPattern(raf, SUBSTATION, firstIed, dttPos);
            if (subName < 0) {
                throw new IOException("未找到 SubStation IED");
            }
            long subStart = findIedOpenBefore(raf, subName, firstIed);
            long subEnd = findIedClose(raf, subStart, dttPos);
            if (subEnd < 0) {
                long nextIed = findPattern(raf, IED_OPEN, subStart + 5, dttPos);
                subEnd = nextIed >= 0 ? nextIed : dttPos;
            } else {
                subEnd += IED_CLOSE.length;
            }
            if (subEnd - subStart > BLOATED_IED) {
                sliceBloatedSubstation(raf, out, firstIed, subStart, dttPos, len);
            } else {
                copyRange(raf, out, 0, firstIed);
                copyRange(raf, out, subStart, subEnd);
                copyRange(raf, out, dttPos, len);
            }
        }
        if (dst.length() < 1000) {
            throw new IOException("裁剪输出过小: " + dst.length());
        }
    }

    private static void sliceBloatedSubstation(
            RandomAccessFile raf,
            OutputStream out,
            long firstIed,
            long subStart,
            long dttPos,
            long len)
            throws IOException {
        int cap = (int) Math.min(8 * 1024 * 1024, dttPos - subStart);
        byte[] chunk = new byte[cap];
        raf.seek(subStart);
        raf.readFully(chunk);
        String text = new String(chunk, StandardCharsets.UTF_8);
        int iedTag = text.indexOf("<IED");
        int iedEnd = text.indexOf('>', iedTag);
        if (iedTag < 0 || iedEnd < 0) {
            throw new IOException("无法解析 SubStation IED 起始标签");
        }
        String iedOpen = text.substring(iedTag, iedEnd + 1);
        Matcher dm = DS_COMM.matcher(text);
        if (!dm.find()) {
            throw new IOException("未找到 dsCommState DataSet");
        }
        String dataSet = dm.group();
        Matcher ln = LN_TYPE.matcher(text.substring(0, dm.start()));
        String lnType = null;
        while (ln.find()) {
            lnType = ln.group(1);
        }
        if (lnType == null) {
            throw new IOException("未找到 LLN0 lnType");
        }
        String rebuilt =
                iedOpen
                        + "\n\t\t<AccessPoint desc=\"站控层\" name=\"S1\">\n"
                        + "\t\t\t<Server timeout=\"30\">\n"
                        + "\t\t\t\t<Authentication/>\n"
                        + "\t\t\t\t<LDevice desc=\"子站\" inst=\"SubStationLD0\">\n"
                        + "\t\t\t\t\t<LN0 desc=\"管理逻辑节点\" inst=\"\" lnClass=\"LLN0\" lnType=\""
                        + lnType
                        + "\">\n\t\t\t\t\t\t"
                        + dataSet
                        + "\n\t\t\t\t\t</LN0>\n"
                        + "\t\t\t\t</LDevice>\n"
                        + "\t\t\t</Server>\n"
                        + "\t\t</AccessPoint>\n"
                        + "\t</IED>\n";
        copyRange(raf, out, 0, firstIed);
        out.write(rebuilt.getBytes(StandardCharsets.UTF_8));
        copyRange(raf, out, dttPos, len);
    }

    private static long findIedOpenBefore(RandomAccessFile raf, long namePos, long minPos) throws IOException {
        long from = Math.max(minPos, namePos - BACKSCAN);
        int n = (int) (namePos - from);
        if (n <= 0) {
            return minPos;
        }
        byte[] buf = new byte[n];
        raf.seek(from);
        raf.readFully(buf);
        for (int i = n - IED_OPEN.length; i >= 0; i--) {
            if (startsAt(buf, i, IED_OPEN)) {
                return from + i;
            }
        }
        return findPattern(raf, IED_OPEN, minPos, namePos + 1);
    }

    private static void copyRange(RandomAccessFile raf, OutputStream out, long start, long end)
            throws IOException {
        raf.seek(start);
        long remain = end - start;
        byte[] buf = new byte[IO_BUF];
        while (remain > 0) {
            int n = raf.read(buf, 0, (int) Math.min(buf.length, remain));
            if (n <= 0) {
                break;
            }
            out.write(buf, 0, n);
            remain -= n;
        }
    }

    private static long findPatternFromEnd(File src, byte[] pat, int tailBytes) throws IOException {
        try (RandomAccessFile raf = new RandomAccessFile(src, "r")) {
            long len = raf.length();
            long start = Math.max(0, len - tailBytes);
            long last = -1;
            long pos = start;
            while (pos < len) {
                long hit = findPattern(raf, pat, pos, len);
                if (hit < 0) {
                    break;
                }
                last = hit;
                pos = hit + 1;
            }
            return last;
        }
    }

    private static long findIedClose(RandomAccessFile raf, long iedStart, long limit) throws IOException {
        long pos = iedStart;
        while (pos < limit) {
            long hit = findPattern(raf, IED_CLOSE, pos, limit);
            if (hit < 0) {
                return -1;
            }
            long after = hit + IED_CLOSE.length;
            if (isLikelyIedTerminator(raf, after, limit)) {
                return hit;
            }
            pos = hit + 1;
        }
        return -1;
    }

    private static boolean isLikelyIedTerminator(RandomAccessFile raf, long pos, long limit)
            throws IOException {
        while (pos < limit) {
            raf.seek(pos);
            int ch = raf.read();
            if (ch < 0) {
                return false;
            }
            if (ch == '<') {
                return true;
            }
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                pos++;
                continue;
            }
            return false;
        }
        return true;
    }

    private static long findPattern(RandomAccessFile raf, byte[] pat, long start, long limit)
            throws IOException {
        int patLen = pat.length;
        if (patLen == 0) {
            return start;
        }
        long end = Math.min(limit, raf.length());
        long pos = start;
        byte[] buf = new byte[IO_BUF];
        byte[] carry = new byte[patLen - 1];
        int carryLen = 0;
        while (pos < end) {
            raf.seek(pos);
            int n = raf.read(buf, 0, (int) Math.min(buf.length, end - pos));
            if (n <= 0) {
                break;
            }
            int hayLen = carryLen + n;
            byte[] hay = new byte[hayLen];
            System.arraycopy(carry, 0, hay, 0, carryLen);
            System.arraycopy(buf, 0, hay, carryLen, n);
            int idx = indexOf(hay, 0, hayLen, pat);
            if (idx >= 0) {
                return pos - carryLen + idx;
            }
            carryLen = Math.min(patLen - 1, hayLen);
            System.arraycopy(hay, hayLen - carryLen, carry, 0, carryLen);
            pos += n;
        }
        return -1;
    }

    private static int indexOf(byte[] hay, int off, int len, byte[] needle) {
        int last = off + len - needle.length;
        for (int i = off; i <= last; i++) {
            if (startsAt(hay, i, needle)) {
                return i;
            }
        }
        return -1;
    }

    private static boolean startsAt(byte[] hay, int off, byte[] needle) {
        for (int i = 0; i < needle.length; i++) {
            if (hay[off + i] != needle[i]) {
                return false;
            }
        }
        return true;
    }

    /** 返回折叠 SubStation IED 在源 SCL/CID 中的 [start, end) 字节偏移；失败返回 null。 */
    public static long[] findSubStationIedBounds(File src) throws IOException {
        long len = src.length();
        long dttPos = findPatternFromEnd(src, DTT_OPEN, TAIL_SEARCH);
        if (dttPos < 0) {
            return null;
        }
        try (RandomAccessFile raf = new RandomAccessFile(src, "r")) {
            long firstIed = findPattern(raf, IED_OPEN, 0, dttPos);
            if (firstIed < 0) {
                return null;
            }
            long subName = findPattern(raf, SUBSTATION, firstIed, dttPos);
            if (subName < 0) {
                return null;
            }
            long subStart = findIedOpenBefore(raf, subName, firstIed);
            long subEnd = findIedClose(raf, subStart, dttPos);
            if (subEnd < 0) {
                long nextIed = findPattern(raf, IED_OPEN, subStart + 5, dttPos);
                subEnd = nextIed >= 0 ? nextIed : dttPos;
            } else {
                subEnd += IED_CLOSE.length;
            }
            return new long[] {subStart, subEnd};
        }
    }
}
