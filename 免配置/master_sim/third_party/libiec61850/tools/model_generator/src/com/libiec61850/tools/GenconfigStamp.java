package com.libiec61850.tools;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.MessageDigest;

/** supervision.cfg.stamp：CID 未变则跳过 genconfig。 */
public final class GenconfigStamp {

    private GenconfigStamp() {}

    public static boolean isUpToDate(File cid, File cfg, File stampFile) {
        if (!cid.isFile() || !cfg.isFile() || !stampFile.isFile()) {
            return false;
        }
        try {
            String expect = cidDigest(cid);
            String got = new String(Files.readAllBytes(stampFile.toPath()), StandardCharsets.UTF_8).trim();
            return expect.equals(got) && cfg.length() > 1000;
        } catch (Exception e) {
            return false;
        }
    }

    public static void writeStamp(File cid, File stampFile) throws IOException {
        try {
            String digest = cidDigest(cid);
            Files.write(stampFile.toPath(), (digest + "\n").getBytes(StandardCharsets.UTF_8));
        } catch (Exception e) {
            throw new IOException("writeStamp failed", e);
        }
    }

    static String cidDigest(File cid) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] buf = new byte[1 << 20];
        try (java.io.FileInputStream in = new java.io.FileInputStream(cid)) {
            int n;
            while ((n = in.read(buf)) > 0) {
                md.update(buf, 0, n);
            }
        }
        byte[] h = md.digest();
        StringBuilder sb = new StringBuilder(h.length * 2);
        for (byte b : h) {
            sb.append(String.format("%02x", b));
        }
        return sb.toString();
    }
}
