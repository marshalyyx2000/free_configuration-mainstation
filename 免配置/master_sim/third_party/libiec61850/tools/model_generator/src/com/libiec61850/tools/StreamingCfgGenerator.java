package com.libiec61850.tools;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.libiec61850.scl.SclParserException;

/**
 * full-ln 路径：slice 本体 + 流式拼入各 LD（FCDA 引用的 LN），再 DynamicModelGenerator。
 */
public final class StreamingCfgGenerator {

    private static final Pattern DS_COMM =
            Pattern.compile("<DataSet[^>]*name=\"dsCommState\"[^>]*>[\\s\\S]*?</DataSet>");

    private StreamingCfgGenerator() {}

    public static void generate(File src, File outCfg) throws Exception {
        File mini = File.createTempFile("supervision_full_ln_", ".scl");
        try {
            long t0 = System.currentTimeMillis();
            SclGenconfigSlice.slice(src, mini);
            byte[] miniBytes = Files.readAllBytes(mini.toPath());
            String miniText = new String(miniBytes, StandardCharsets.UTF_8);
            Matcher dm = DS_COMM.matcher(miniText);
            String dsComm = dm.find() ? dm.group() : "";
            byte[] augmented = FullLnLDevicePruner.augmentMiniWithFullLnDevices(src, miniBytes, dsComm);
            Files.write(mini.toPath(), augmented);
            System.out.println(
                    "slice+full-ln "
                            + (System.currentTimeMillis() - t0)
                            + " ms, mini "
                            + mini.length()
                            + " bytes");
            runDynamicGenerator(mini, outCfg);
        } finally {
            mini.delete();
        }
    }

    private static void runDynamicGenerator(File mini, File outCfg) throws Exception {
        if (outCfg.exists() && !outCfg.delete()) {
            System.err.println("WARN: could not delete old " + outCfg.getAbsolutePath());
        }
        InputStream stream = new FileInputStream(mini);
        PrintStream outputStream = new PrintStream(new FileOutputStream(outCfg));
        int genErr = 0;
        try {
            new DynamicModelGenerator(stream, mini.getAbsolutePath(), outputStream, "SubStation", "S1");
        } catch (SclParserException e) {
            genErr = 1;
            System.err.println("WARN DynamicModelGenerator: " + e.getMessage());
        } catch (RuntimeException e) {
            genErr = 1;
            System.err.println("WARN DynamicModelGenerator: " + e.getClass().getSimpleName() + ": " + e.getMessage());
        } finally {
            outputStream.close();
            stream.close();
        }
        if (!outCfg.isFile() || outCfg.length() == 0) {
            throw new IllegalStateException("empty supervision.cfg");
        }
        CfgLphdAugmentor.ensureCommStateProxy(outCfg);
        if (genErr != 0 && outCfg.length() > 500_000) {
            System.err.println("WARN: ignore generator error, cfg size=" + outCfg.length());
            genErr = 0;
        }
        if (genErr != 0 && outCfg.length() <= 500_000) {
            throw new IllegalStateException("DynamicModelGenerator failed");
        }
    }
}
