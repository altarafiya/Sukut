package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class RecordingCapturePolicyTest {
    private fun dump(criteria: String, flags: String = "MIX_ROUTE_FLAG_RENDER|MIX_ROUTE_FLAG_LOOP_BACK", type: String = "MIX_TYPE_PLAYERS") = """
        Audio Policy Mix:
          Audio Policy Mix 2 (0x123):
          - mix type: $type
          - Route Flags: $flags
          - device address: recorder:ap:1mixp:0
          $criteria
        Audio sources (0):
    """.trimIndent()
    private val normal = """
        - Criterion 0: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_MEDIA
        - Criterion 1: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_UNKNOWN
        - Criterion 2: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_GAME
    """.trimIndent()

    @Test fun readsPlaybackCaptureAndPreservesEveryUsageForRestoration() {
        val text = dump(normal + "\n- Criterion 3: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_ALARM\n- Criterion 4: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_VOICE_COMMUNICATION")
        val snapshot = RecordingCapturePolicy.parse(text).values.single()
        assertEquals("recorder:ap:1mixp:0", snapshot.address)
        assertEquals(listOf(0, 1, 2, 4, 14), snapshot.usages)
        assertNull(snapshot.uid)
    }
    @Test fun neverTreatsTheRawEngineCaptureAsARecorder() {
        assertTrue(RecordingCapturePolicy.parse(dump(normal, "MIX_ROUTE_FLAG_LOOP_BACK")).isEmpty())
        assertTrue(RecordingCapturePolicy.parse(dump(normal, type = "MIX_TYPE_RECORDERS")).isEmpty())
    }
    @Test fun unknownOrExclusionRulesAreNotSilentlyDiscarded() {
        for (extra in listOf("RULE_EXCLUDE_UID 2000", "RULE_MATCH_USERID 0", "RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_FUTURE", "RULE_MATCH_AUDIO_SESSION_ID 12"))
            assertTrue(RecordingCapturePolicy.parse(dump(normal + "\n- Criterion 3: " + extra)).isEmpty())
    }
    @Test fun identifiesOnlyOurExactUidRestrictionForSafeRestoration() {
        val snapshot = RecordingCapturePolicy.parse(dump(normal + "\n- Criterion 3: RULE_MATCH_UID 2000")).values.single()
        assertEquals(Integer.valueOf(2000), snapshot.uid)
        assertEquals(listOf(0, 1, 14), snapshot.usages)
        assertTrue(RecordingCapturePolicy.parse(dump(normal + "\n- Criterion 3: RULE_MATCH_UID 2000\n- Criterion 4: RULE_MATCH_UID 1000")).isEmpty())
    }
    @Test fun handlesCrLfAndDoesNotBroadenARecorderThatExcludedOurMediaOutput() {
        assertEquals(1, RecordingCapturePolicy.parse(dump(normal).replace("\n", "\r\n")).size)
        assertTrue(RecordingCapturePolicy.parse(dump("- Criterion 0: RULE_MATCH_ATTRIBUTE_USAGE AUDIO_USAGE_GAME")).isEmpty())
        assertTrue(RecordingCapturePolicy.parse(dump(normal).replace("recorder:ap:1mixp:0", "")).isEmpty())
    }
}
