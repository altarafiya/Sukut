package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class EnginePerformanceConfigTest {
    @Test fun presetPairsAndCustomMapping() {
        assertEquals(EnginePerformanceConfig.Options(false, 0), EnginePerformanceConfig.presets[0])
        assertEquals(EnginePerformanceConfig.Options(false, 1), EnginePerformanceConfig.presets[1])
        assertEquals(EnginePerformanceConfig.Options(true, 1), EnginePerformanceConfig.presets[2])
        EnginePerformanceConfig.presets.forEachIndexed { index, pair ->
            assertEquals(index, EnginePerformanceConfig.mode(pair.effects, pair.buffering))
            assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformanceConfig.mode(pair.effects, pair.buffering, true))
        }
        assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformanceConfig.mode(false, 2))
        assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformanceConfig.mode(true, 0))
        assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformanceConfig.mode(true, 2))
    }
    @Test fun backgroundRangeClampsLegacyValues() {
        for (value in listOf(-100, -21, -20, -10, 0, 10, 11, 100))
            assertEquals(value.coerceIn(-20, 10), EnginePerformanceConfig.background(value))
    }
}
