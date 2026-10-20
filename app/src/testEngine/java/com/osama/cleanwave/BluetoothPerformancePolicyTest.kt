package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class BluetoothPerformancePolicyTest {
    private val speaker = BluetoothPerformancePolicy.Options(false, 0, false)
    private val recommended = BluetoothPerformancePolicy.Options(true, 1, true)

    @Test fun bluetoothAppliesRecommendedCustomAndKeepsOriginalSettings() {
        val change = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "8:42", true, speaker)
        assertEquals(recommended, change.options)
        assertEquals(speaker, change.state.previous)
        assertEquals("8:42", change.state.device)
    }

    @Test fun repeatedRouteAndEngineRestartRespectManualRawOverride() {
        val connected = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "8:42", true, speaker)
        val manual = BluetoothPerformancePolicy.Options(false, 2, true)
        repeat(5) {
            val unchanged = BluetoothPerformancePolicy.observe(connected.state, "8:42", true, manual)
            assertEquals(manual, unchanged.options)
            assertEquals(connected.state, unchanged.state)
        }
    }

    @Test fun speakerOrWiredReturnRestoresAllOriginalOptions() {
        val original = BluetoothPerformancePolicy.Options(false, 2, true)
        val connected = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "26:42", true, original)
        for (route in listOf("2:1", "3:8", "22:9")) {
            val disconnected = BluetoothPerformancePolicy.observe(connected.state, route, false, recommended)
            assertEquals(original, disconnected.options)
            assertEquals(BluetoothPerformancePolicy.State(), disconnected.state)
        }
    }

    @Test fun switchingBluetoothDevicesKeepsTheSpeakerSnapshot() {
        val first = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "8:42", true, speaker)
        val manual = BluetoothPerformancePolicy.Options(false, 2, true)
        val second = BluetoothPerformancePolicy.observe(first.state, "27:43", true, manual)
        assertEquals(recommended, second.options)
        assertEquals(speaker, second.state.previous)
        val restored = BluetoothPerformancePolicy.observe(second.state, "2:1", false, second.options)
        assertEquals(speaker, restored.options)
    }

    @Test fun reconnectAfterDisconnectStartsANewBluetoothSession() {
        val first = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "8:42", true, speaker)
        val disconnected = BluetoothPerformancePolicy.observe(first.state, "2:1", false, recommended)
        val reconnected = BluetoothPerformancePolicy.observe(disconnected.state, "8:42", true, disconnected.options)
        assertEquals(recommended, reconnected.options)
        assertEquals(speaker, reconnected.state.previous)
    }

    @Test fun ordinaryOutputChangesPreserveUserSettings() {
        val change = BluetoothPerformancePolicy.observe(BluetoothPerformancePolicy.State(), "3:8", false, speaker)
        assertEquals(speaker, change.options)
        assertEquals(BluetoothPerformancePolicy.State(), change.state)
    }
}
