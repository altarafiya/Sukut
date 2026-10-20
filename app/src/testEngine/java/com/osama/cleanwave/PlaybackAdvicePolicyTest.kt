package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class PlaybackAdvicePolicyTest {
    @Test fun startupAndOneCounterBurstDoNotWarn() {
        val policy = PlaybackAdvicePolicy()
        assertFalse(policy.observe(0, 0, 0, 0))
        assertFalse(policy.observe(1000, 50, 6, 30))
        assertFalse(policy.observe(9000, 450, 6, 30)) // suspended loop / transition
        assertFalse(policy.observe(10000, 500, 6, 30))
    }
    @Test fun separateRecurringEpisodesWarnThenExpire() {
        val policy = PlaybackAdvicePolicy()
        policy.observe(0, 0, 0, 0)
        for (n in 1..11) policy.observe(n * 1000L, n * 50L, 0, 0)
        assertFalse(policy.observe(12000, 600, 1, 0))
        assertFalse(policy.observe(13000, 650, 1, 0))
        assertFalse(policy.observe(14000, 700, 2, 0))
        assertFalse(policy.observe(15000, 750, 2, 0))
        assertFalse(policy.observe(16000, 800, 3, 0))
        assertFalse(policy.observe(17000, 850, 3, 0))
        assertFalse(policy.observe(18000, 900, 4, 0))
        assertFalse(policy.observe(19000, 950, 4, 0))
        assertTrue(policy.observe(20000, 1000, 5, 0))
        for (n in 21..66) policy.observe(n * 1000L, n * 50L, 5, 0)
        assertFalse(policy.observe(67000, 3350, 5, 0))
    }
    @Test fun idleAndCounterResetDoNotWarn() {
        val policy = PlaybackAdvicePolicy()
        policy.observe(0, 0, 0, 0)
        for (n in 1..20) assertFalse(policy.observe(n * 1000L, 0, n.toLong(), n * 5L))
        assertFalse(policy.observe(21000, 50, 20, 100))
        assertFalse(policy.observe(22000, 0, 0, 0))
    }
    @Test fun underrunsNeedRepeatedWindowsAndDuration() {
        val policy = PlaybackAdvicePolicy()
        var packets = 0L
        var underruns = 0L
        var time = 0L
        policy.observe(0, packets, 0, 0)
        // Poll past the grace period without any trouble.
        while (time < 12000) { time += 2000; packets += 100; assertFalse(policy.observe(time, packets, 0, 0)) }
        // Sustained underruns: warn only after enough windows over enough time.
        for (i in 1..11) { time += 2000; packets += 100; underruns += 10; assertFalse(policy.observe(time, packets, 0, underruns)) }
        time += 2000; packets += 100; underruns += 10
        assertTrue(policy.observe(time, packets, 0, underruns))
    }
}
