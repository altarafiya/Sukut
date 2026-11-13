package com.osama.cleanwave;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** One startup read. A detached Unix session can still inherit adbd's kill cgroup. */
final class WorkerLifetime {
    static boolean debuggingTethered(int pid) {
        try { return inheritedServiceGroup(new String(Files.readAllBytes(Paths.get("/proc/self/cgroup"))), pid); }
        catch (Exception ignored) { return true; } // Unknown must not promise survival.
    }
    static boolean inheritedServiceGroup(String groups, int ownPid) {
        if (groups.trim().isEmpty()) return true;
        Pattern service = Pattern.compile("/pid_(\\d+)(?:/|$)");
        for(String line:groups.split("\\n")) {
            Matcher match=service.matcher(line);
            while(match.find()) if(!match.group(1).equals(Integer.toString(ownPid))) return true;
        }
        return false;
    }
}
