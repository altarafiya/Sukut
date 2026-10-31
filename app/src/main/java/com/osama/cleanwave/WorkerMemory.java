package com.osama.cleanwave;

/** Returns the audio worker's freed native memory to Android at Off. Loaded only in the worker. */
final class WorkerMemory {
    private static boolean loaded;
    private WorkerMemory() { }

    static boolean purge() {
        try {
            if (!loaded) { System.loadLibrary("sukut_memory"); loaded = true; }
            return purge0();
        } catch (Throwable unavailable) {
            return false; // Optional: Off still works and memory is reused at the next Start.
        }
    }

    private static native boolean purge0();
}
