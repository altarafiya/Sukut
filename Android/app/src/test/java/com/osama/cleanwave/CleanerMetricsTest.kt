package com.osama.cleanwave
import org.junit.Assert.*
import org.junit.Test
class CleanerMetricsTest {
    @Test fun reserveAbsorbsShortJitterButStillDetectsSustainedOverload() {
        val stats = CleanerMetrics("buffered", 20.0, fallbackOnBacklog = false,
            recoveryCooldownMs = 40.0).apply { overloadCreditMs = 60.0 }
        repeat(3) {
            for (ms in listOf(28L, 28L, 8L, 8L)) {
                stats.add(ms * 1_000_000, 0, stats.nextWork(0))
                assertFalse("Buffered scheduler jitter must not interrupt the model", stats.paused)
            }
        }
        repeat(12) { if (!stats.paused) stats.add(27_000_000, 0, stats.nextWork(0)) }
        assertTrue("A sustained deficit must still trigger recovery", stats.paused)
    }
    @Test fun sustainedOverloadFallsBackThenRecoversAutomatically() {
        val stats=CleanerMetrics("test",10.0,recoveryCooldownMs=20.0,requiredRecoveryProbes=2)
        repeat(4){
            val work=stats.nextWork(0);assertEquals(CleanerWork.RUN,work);stats.add(5_000_000,0,work)
        }
        var work=stats.nextWork(0);stats.add(15_000_000,0,work);assertFalse(stats.paused)
        work=stats.nextWork(0);stats.add(15_000_000,0,work);assertTrue(stats.paused)

        assertEquals(CleanerWork.BYPASS,stats.nextWork(0))
        assertEquals(CleanerWork.BYPASS,stats.nextWork(0))
        work=stats.nextWork(0);assertEquals(CleanerWork.RECOVERY_PROBE,work);stats.add(5_000_000,0,work);assertTrue(stats.paused)
        work=stats.nextWork(0);assertEquals(CleanerWork.RECOVERY_PROBE,work);stats.add(5_000_000,0,work);assertFalse(stats.paused)
        assertTrue(stats.report(1f).contains("recoveries=1"))
    }

    @Test fun fullCaptureBacklogEntersFallbackBeforeRunningMoreAi() {
        val stats=CleanerMetrics("test",10.0,recoveryCooldownMs=10.0)
        assertEquals(CleanerWork.FALLBACK_FADE,stats.nextWork(2))
        assertTrue(stats.paused)
    }

    @Test fun timingOnlyModeIgnoresQueueOccupancyButStillRecoversFromSlowInference() {
        val stats=CleanerMetrics(
            "test",10.0,recoveryCooldownMs=20.0,requiredRecoveryProbes=2,fallbackOnBacklog=false
        )
        repeat(3) {
            val work=stats.nextWork(3);assertEquals(CleanerWork.RUN,work);stats.add(5_000_000,3,work)
        }
        var work=stats.nextWork(3);stats.add(15_000_000,3,work);assertFalse(stats.paused)
        work=stats.nextWork(3);stats.add(15_000_000,3,work);assertTrue(stats.paused)
        assertEquals(CleanerWork.BYPASS,stats.nextWork(3))
        assertEquals(CleanerWork.BYPASS,stats.nextWork(3))
        work=stats.nextWork(3);assertEquals(CleanerWork.RECOVERY_PROBE,work);stats.add(5_000_000,3,work)
        work=stats.nextWork(3);assertEquals(CleanerWork.RECOVERY_PROBE,work);stats.add(5_000_000,3,work)
        assertFalse(stats.paused)
    }
}
