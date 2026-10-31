package com.osama.cleanwave

import android.content.Context
import android.graphics.Typeface
import android.text.SpannableString
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan

internal object TextAccents {
    fun color(context: Context, value: CharSequence): CharSequence {
        // Quiet accent colours; preserve whole Arabic words and their joining.
        val palette = UiPalette(context)
        val green = palette.accent
        val gold = palette.highlight
        val blue = palette.onAccentContainer
        val result = SpannableString(value)
        val plain = value.toString()
        listOf("Developer options" to blue,"Build number" to blue,"OS version" to blue,"seven times" to palette.warning,
            "Wireless debugging" to blue,"USB debugging" to blue,"pairing code" to palette.warning,"Activate" to blue,"Ready" to ModeStyle.buffer(0,palette.dark),
            "خيارات المطوّر" to blue,"رقم الإصدار" to blue,"إصدار النظام" to blue,"سبع مرات" to palette.warning,
            "التصحيح اللاسلكي" to blue,"تصحيح USB" to blue,"رمز الإقران" to palette.warning,"تفعيل" to blue,"جاهز" to ModeStyle.buffer(0,palette.dark),
            "Sukut" to gold, "سُكوت" to gold, "Dolby" to blue,
            "internet" to blue, "الإنترنت" to blue, "Without the music" to gold, "بلا موسيقى" to gold,
            context.getString(R.string.voice) to green, context.getString(R.string.music) to gold,
            "Music muted" to gold, "Music and background muted" to gold, "الموسيقى مكتومة" to gold, "الموسيقى والخلفية مكتومتان" to gold, "delay" to gold, "تأخير" to gold, "faster" to blue, "أسرع" to blue, "steadier" to green, "أثبت" to green, "phone effects" to blue, "مؤثرات الهاتف" to blue, "Bluetooth" to blue, "بلوتوث" to blue, "Recommended" to green, "موصى به" to green).forEach { (word, color) ->
            var start = plain.indexOf(word, ignoreCase = true)
            while (start >= 0) {
                val end = start + word.length
                val arabic = ArabicAccentRange.containsArabic(word)
                if(!arabic && ((start>0 && plain[start-1].isLetterOrDigit()) || (end<plain.length && plain[end].isLetterOrDigit()))) {
                    start=plain.indexOf(word,end,ignoreCase=true); continue
                }
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
