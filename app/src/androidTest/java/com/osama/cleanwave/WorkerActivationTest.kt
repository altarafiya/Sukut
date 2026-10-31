package com.osama.cleanwave

import android.net.Uri
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Requires Windows/ADB activation first. Does not capture or start audio. */
@RunWith(AndroidJUnit4::class)
class WorkerActivationTest {
    @Test fun activationExposesOnlyVersionedStatusAndLeavesAudioOff() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue("Activate Sukut from Windows first", WorkerConnection.ready)
        val result = requireNotNull(context.contentResolver.call(Uri.parse("content://com.altarafiyah.sukut.worker"), "status", null, null))
        assertEquals("READY", result.getString("state"))
        assertEquals(BuildConfig.VERSION_CODE, result.getInt("version"))
        assertEquals(1, result.getInt("protocol"))
        assertTrue(result.getInt("pid") > 0)
        assertNull(result.getBinder("worker"))
        assertFalse("Begin this test with audio Off", requireNotNull(WorkerConnection.api).isRunning)
    }
    @Test fun offKeepsTheSameActivatedEndpoint() {
        val api = requireNotNull(WorkerConnection.api) { "Activate from Windows first" }
        assertFalse("Begin this test with audio Off", api.isRunning)
        val pid = WorkerConnection.workerPid
        api.stopProcessing()
        assertTrue(WorkerConnection.ready)
        assertEquals(pid, WorkerConnection.workerPid)
        assertFalse(api.isRunning)
    }
}
