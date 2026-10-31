package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class WorkerProtocolTest {
    @Test fun onlyAdbShellCanBootstrap() {
        assertTrue(WorkerProtocol.trustedBootstrap(2000))
        listOf(0,1000,10001,99000,-1).forEach { assertFalse(WorkerProtocol.trustedBootstrap(it)) }
    }
    @Test fun otherAppsCannotControlTheAudioWorkerEvenIfTheyObtainItsBinder() {
        assertTrue(WorkerProtocol.trustedController(10123,10123,2000))
        assertTrue(WorkerProtocol.trustedController(2000,10123,2000))
        listOf(10124,110123,1000,99000).forEach { assertFalse(WorkerProtocol.trustedController(it,10123,2000)) }
    }
    @Test fun handshakeRequiresSameProtocolAndAppBuild() {
        assertTrue(WorkerProtocol.compatible(1,BuildConfig.VERSION_CODE))
        assertFalse(WorkerProtocol.compatible(2,BuildConfig.VERSION_CODE))
        assertFalse(WorkerProtocol.compatible(1,BuildConfig.VERSION_CODE-1))
    }
}
