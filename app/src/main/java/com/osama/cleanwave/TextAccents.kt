package com.osama.cleanwave

import android.content.Context
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan

internal object TextAccents {
    fun color(context: Context, value: CharSequence): CharSequence {
        val dark = UiPrefs.isDark(context)
        val green = if (dark) 0xFF60CE93.toInt() else 0xFF247C50.toInt()
        val gold = if (dark) 0xFFE2BA77.toInt() else 0xFF92621E.toInt()
        val blue = if (dark) 0xFF80B5FF.toInt() else 0xFF296BC7.toInt()
        val result = SpannableString(value)
        val plain = value.toString()
        listOf("Sukut" to gold, "سُكوت" to gold, "Shizuku" to blue, "Dolby" to blue,
            "internet" to blue, "الإنترنت" to blue, "Without the music" to gold, "بلا موسيقى" to gold,
            context.getString(R.string.voice) to green, context.getString(R.string.music) to gold,
            "Music muted" to gold, "Music and background muted" to gold, "الموسيقى مكتومة" to gold, "الموسيقى والخلفية مكتومتان" to gold, "delay" to gold, "تأخير" to gold, "faster" to blue, "أسرع" to blue, "steadier" to green, "أثبت" to green, "phone effects" to blue, "مؤثرات الهاتف" to blue, "Bluetooth" to blue, "بلوتوث" to blue, "Recommended" to green, "موصى به" to green).forEach { (word, color) ->
            var start = plain.indexOf(word, ignoreCase = true)
            while (start >= 0) {
                val end = start + word.length
                val arabic = ArabicAccentRange.containsArabic(word)
                val range = if (arabic) ArabicAccentRange.wholeWords(value, start, end) else start until end
                result.setSpan(ForegroundColorSpan(color), range.first, range.last + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                // Keep the same face and weight across Arabic text. Synthetic
                // bold starting after the article الـ broke contextual joining.
                if (!arabic) result.setSpan(StyleSpan(Typeface.BOLD), range.first, range.last + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
                start = plain.indexOf(word, end, ignoreCase = true)
            }
        }
        return result
    }
}
