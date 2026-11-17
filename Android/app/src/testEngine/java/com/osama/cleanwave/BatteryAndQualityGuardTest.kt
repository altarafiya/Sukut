package com.osama.cleanwave

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BatteryAndQualityGuardTest {
    @Test
    fun standbyNeedsHoldAndZeroOutputAndLeavesOnOneLsb() {
        val standby = SilenceStandby(holdPackets = 3)
        repeat(2) { assertFalse(standby.before(true)); standby.after(true) }
        assertFalse(standby.active)
        assertFalse(standby.before(true))
        standby.after(false) // processed tail still audible: keep working
        assertFalse(standby.active)
        assertFalse(standby.before(true))
        standby.after(true)
        assertTrue(standby.active)
        assertTrue(standby.before(true))
        assertEquals(1L, standby.skippedPackets)
        // Any nonzero sample is processed in that same packet.
        assertFalse(standby.before(false))
        assertFalse(standby.active)
        assertEquals(1L, standby.entries)
    }

    @Test
    fun allZeroDetectsSingleLsb() {
        val packet = ByteArray(3840)
        assertTrue(SilenceStandby.allZero(packet))
        packet[3839] = 1
        assertFalse(SilenceStandby.allZero(packet))
    }

    private fun band(value: Float) = FloatArray(1024).also {
        for (bin in HighBandGuard.REFERENCE_START until HighBandGuard.REFERENCE_END) it[bin * 2] = value
    }

    @Test
    fun highBandFollowsMusicShareWithFloorAndRecovers() {
        val guard = HighBandGuard()
        val zero = FloatArray(1024)
        val music = band(1f)
        repeat(20) { guard.update(music, music, zero, zero, music, music, enabled = true) }
        assertEquals(HighBandGuard.FLOOR, guard.gain, 1e-3f)
        val voice = band(1f)
        repeat(60) { guard.update(voice, voice, voice, voice, zero, zero, enabled = true) }
        assertTrue("speech air must return", guard.gain > .99f)
    }

    @Test
    fun highBandHoldsWithoutEvidenceAndResetsWhenDisabled() {
        val guard = HighBandGuard()
        val zero = FloatArray(1024)
        val music = band(1f)
        guard.update(music, music, zero, zero, music, music, enabled = true)
        val held = guard.gain
        // Residual-only band: the heads explain nothing, so keep the last decision.
        guard.update(band(1f), band(1f), zero, zero, zero, zero, enabled = true)
        assertEquals(held, guard.gain, 0f)
        guard.update(music, music, zero, zero, music, music, enabled = false)
        assertEquals(1f, guard.gain, 0f)
    }

    @Test
    fun relativeErrorRejectsNonFiniteAndScalesByPeak() {
        assertEquals(.01f, EngineSelection.relativeError(floatArrayOf(100f, 0f), floatArrayOf(99f, 0f)), 1e-6f)
        assertTrue(EngineSelection.relativeError(floatArrayOf(1f), floatArrayOf(Float.NaN)).isInfinite())
    }
}
