package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class ModeStyleTest {
    @Test fun audioSharesBlueAndBufferingKeepsDistinctReadableColors() {
        for (dark in listOf(true, false)) {
            val base = if (dark) 0xFF242424.toInt() else 0xFFF2F2F2.toInt()
            val audio = listOf(2, 0, 3).map { ModeStyle.audio(it, dark) }
            val buffer = (0..2).map { ModeStyle.buffer(it, dark) }
            assertEquals(1, audio.distinct().size); assertEquals(3, buffer.distinct().size)
            (audio + buffer).forEach { assertTrue(ThemeColors.contrast(it, base) >= 4.5) }
        }
        assertEquals(3, (0..2).map(ModeStyle::bufferIcon).distinct().size)
    }
}
