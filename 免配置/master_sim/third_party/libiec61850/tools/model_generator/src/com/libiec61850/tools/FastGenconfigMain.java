package com.libiec61850.tools;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.PrintStream;
import java.nio.file.Files;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.libiec61850.scl.SclParserException;

/**
 * 兼容 router 的 genconfig 入口：裁剪 CID/SMCD（头+SubStation 全量+模板），再对 SubStation 调 DynamicModelGenerator。
 */
public class FastGenconfigMain {

    private static final Pattern DS_COMM =
            Pattern.compile("<DataSet[^>]*name=\"dsCommState\"[^>]*>[\\s\\S]*?</DataSet>");

    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("Usage: genconfig <SCL/CID file> [<output filename>]");
            System.exit(1);
        }
        File src = new File(args[0]);
        File outCfg = new File(args[1]);
        File mini = File.createTempFile("supervision_mini_", ".scl");
        try {
            long t0 = System.currentTimeMillis();
            SclGenconfigSlice.slice(src, mini);
            byte[] miniBytes = Files.readAllBytes(mini.toPath());
            String miniText = new String(miniBytes, java.nio.charset.StandardCharsets.UTF_8);
            Matcher dm = DS_COMM.matcher(miniText);
            String dsComm = dm.find() ? dm.group() : "";
            byte[] augmented =
                    CollapsedLDevicePruner.augmentMiniWithCollapsedDevices(src, miniBytes, dsComm);
            Files.write(mini.toPath(), augmented);
            System.out.println(
                    "slice " + (System.currentTimeMillis() - t0) + " ms, mini "
                            + mini.length() + " bytes (augmented)");
            System.out.println("Dynamic model generator (fast slice + collapsed LD prune)");
            InputStream stream = new FileInputStream(mini);
            PrintStream outputStream = new PrintStream(new FileOutputStream(outCfg));
            int genErr = 0;
            try {
                new DynamicModelGenerator(stream, mini.getAbsolutePath(), outputStream, "SubStation", "S1");
            } catch (SclParserException e) {
                genErr = 1;
                System.err.println("WARN DynamicModelGenerator: " + e.getMessage());
            } finally {
                outputStream.close();
                stream.close();
            }
            if (!outCfg.isFile() || outCfg.length() == 0) {
                System.err.println("ERROR: empty supervision.cfg");
                System.exit(1);
            }
            CfgLphdAugmentor.ensureCommStateProxy(outCfg);
            if (genErr != 0 && outCfg.length() > 500_000) {
                System.err.println("WARN: ignore generator error, cfg size=" + outCfg.length());
                genErr = 0;
            }
            if (genErr != 0) {
                System.exit(1);
            }
        } finally {
            mini.delete();
        }
    }
}
