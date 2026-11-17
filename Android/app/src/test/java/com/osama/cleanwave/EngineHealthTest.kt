package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class EngineHealthTest {
    @Test fun homeRefreshesForVisibleStateButIgnoresUnrelatedPreferences() {
        for (key in listOf("enabled", "status", "error", "audio_source_preset", "buffering", "effects_compatibility",
            "engine_performance_custom", "optimized_background_db", "recovery_until", "overload_tip", "bluetooth_output_key"))
            assertTrue(key, ControlPlanePolicy.affectsHome(key))
        for (key in listOf("metrics", "restore_on_boot", "amplification_db", "bluetooth_previous_effects"))
            assertFalse(key, ControlPlanePolicy.affectsHome(key))
    }
    @Test fun stoppedWorkersKeepTheirFailureReason() {
        val reply = EngineHealth.parse(arrayOf("false", "Audio route stopped", "packets=8"))
        assertFalse(reply.running)
        assertEquals("Audio route stopped", reply.detail)
        assertEquals("packets=8", reply.metrics)
    }
    @Test fun activeSnapshotsPreserveRecoveryCounters() {
        val reply = EngineHealth.parse(arrayOf("true", "active", "fallback_events=2; recoveries=2"))
        assertTrue(reply.running)
        assertEquals("fallback_events=2; recoveries=2", reply.metrics)
    }
    @Test fun malformedRepliesCannotPretendProcessingIsHealthy() {
        for (reply in listOf(emptyArray(), arrayOf("true"), arrayOf("yes", "active", ""))) {
            assertThrows(IllegalArgumentException::class.java) { EngineHealth.parse(reply) }
        }
    }
}
