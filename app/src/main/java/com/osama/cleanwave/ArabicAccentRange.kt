package com.osama.cleanwave

/** A colour/weight boundary must never cut through an Arabic joining word. */
internal object ArabicAccentRange {
    fun containsArabic(value: CharSequence) = value.any {
        Character.UnicodeScript.of(it.code) == Character.UnicodeScript.ARABIC
    }

    fun wholeWords(value: CharSequence, start: Int, end: Int): IntRange {
        fun word(ch: Char): Boolean = Character.isLetterOrDigit(ch) || when (Character.getType(ch)) {
            Character.NON_SPACING_MARK.toInt(), Character.COMBINING_SPACING_MARK.toInt(),
            Character.ENCLOSING_MARK.toInt() -> true
            else -> ch == '\u200c' || ch == '\u200d'
        }
        var from = start
        var until = end
        while (from > 0 && word(value[from - 1])) from--
        while (until < value.length && word(value[until])) until++
        return from until until
    }
}
