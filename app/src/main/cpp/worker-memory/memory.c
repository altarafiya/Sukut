#include <jni.h>
#include <malloc.h>

/*
 * The audio worker is started by app_process, not forked from zygote, so its native
 * allocator keeps freed pages after Off (measured: 48 MB in scudo's primary pool).
 * Asks the allocator to return free memory to Android. Called once at Off; never
 * while audio runs, and it frees nothing that is still in use.
 */
JNIEXPORT jboolean JNICALL
Java_com_osama_cleanwave_WorkerMemory_purge0(JNIEnv *env, jclass type) {
    (void) env;
    (void) type;
#ifdef M_PURGE_ALL
    /* Android 14+: also drains per-thread caches. Older versions return 0. */
    if (mallopt(M_PURGE_ALL, 0) == 1) return JNI_TRUE;
#endif
    return mallopt(M_PURGE, 0) == 1 ? JNI_TRUE : JNI_FALSE;
}
