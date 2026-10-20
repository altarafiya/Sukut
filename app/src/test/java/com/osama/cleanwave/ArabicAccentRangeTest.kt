package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class ArabicAccentRangeTest {
    @Test fun keepsArticleAndDiacriticsWithTheAccentedWord() {
        for ((text, match, expected) in listOf(
            Triple("الخيار الموصى به.", "موصى به", "الموصى به"),
            Triple("تزيد التأخير.", "تأخير", "التأخير"),
            Triple("وترشيح الموسيقى يقلّل التقطّع.", "موسيقى", "الموسيقى"),
            Triple("والتَّأخير أقلّ", "أخير", "والتَّأخير")
        )) {
            val start = text.indexOf(match)
            assertTrue(start >= 0)
            val range = ArabicAccentRange.wholeWords(text, start, start + match.length)
            assertEquals(expected, text.substring(range))
        }
    }
    @Test fun leavesPunctuationAndNeighbouringWordsOutsideAccent() {
        val text = "(تأخير)، صوت أوضح"
        assertEquals("تأخير", text.substring(ArabicAccentRange.wholeWords(text, 1, 6)))
        assertTrue(ArabicAccentRange.containsArabic("سُكوت"))
        assertFalse(ArabicAccentRange.containsArabic("Sukut"))
    }
}
