package com.osama.cleanwave;

/** Loaded only during an explicit Wireless debugging pairing attempt. */
final class AdbPairingCrypto {
    static { System.loadLibrary("sukut_pairing"); }
    static native long create(byte[] password);
    static native byte[] message(long handle);
    static native byte[] finish(long handle, byte[] peer);
    static native void destroy(long handle);
}
