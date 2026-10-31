package com.osama.cleanwave;

/** Versioned control channel. Audio never crosses this channel. */
public final class WorkerProtocol {
    public static final int VERSION = 1;
    public static final String PACKAGE = BuildConfig.APPLICATION_ID;
    public static final String AUTHORITY = PACKAGE + ".worker";
    private WorkerProtocol() { }
    public static boolean trustedBootstrap(int uid) { return uid == 2000; }
    public static boolean trustedController(int caller, int owner, int self) {
        return caller == owner || caller == self;
    }
    public static boolean compatible(int protocol, int version) {
        return protocol == VERSION && version == BuildConfig.VERSION_CODE;
    }
}
