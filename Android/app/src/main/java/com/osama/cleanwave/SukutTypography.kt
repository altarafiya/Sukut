package com.osama.cleanwave

import android.content.res.Resources
import android.graphics.Typeface
import android.view.View
import android.view.ViewGroup
import android.widget.TextView

internal object SukutTypography {
    private val fonts = android.util.SparseArray<Typeface>(4)
    @Synchronized private fun font(resources: Resources, arabic: Boolean, bold: Boolean): Typeface {
        val key = (if (arabic) 2 else 0) + (if (bold) 1 else 0)
        return fonts[key] ?: (if (arabic) resources.getFont(
            if (bold) R.font.ibm_plex_arabic_semibold else R.font.ibm_plex_arabic)
        else Typeface.create(resources.getFont(R.font.readex_pro),
            if (bold) Typeface.BOLD else Typeface.NORMAL)).also { fonts.put(key, it) }
    }
    fun apply(view: View) {
        if (view is TextView) {
            // The app can be Arabic while Android stays English. Use its
            // translated context for shaping and paragraph direction.
            view.textLocale = view.resources.configuration.locales[0]
            if (view.textDirection == View.TEXT_DIRECTION_INHERIT || view.textDirection == View.TEXT_DIRECTION_LOCALE)
                view.textDirection = if (view.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL) View.TEXT_DIRECTION_RTL else View.TEXT_DIRECTION_LTR
            val arabic = view.resources.configuration.locales[0].language == "ar" || ArabicAccentRange.containsArabic(view.text)
            val bold = (view.typeface?.weight ?: 400) >= 600
            view.typeface = font(view.resources, arabic, bold)
            if (arabic) {
                // Joining strokes and marks extend beyond the nominal glyph box.
                // Idempotent because status updates apply typography repeatedly.
                view.includeFontPadding = true
                view.isFallbackLineSpacing = true
                view.paint.isFakeBoldText = false
                view.letterSpacing = 0f
                view.setLineSpacing(maxOf(view.lineSpacingExtra, 2 * view.resources.displayMetrics.density), 1f)
                val inset = (3 * view.resources.displayMetrics.density + .5f).toInt()
                val vertical = (2 * view.resources.displayMetrics.density + .5f).toInt()
                view.setPaddingRelative(maxOf(view.paddingStart, inset), maxOf(view.paddingTop, vertical),
                    maxOf(view.paddingEnd, inset), maxOf(view.paddingBottom, vertical))
            }
            if (arabic && android.os.Build.VERSION.SDK_INT >= 35) {
                view.setUseBoundsForWidth(true)
                view.setShiftDrawingOffsetForStartOverhang(true)
            }
        }
        if (view is ViewGroup) for (i in 0 until view.childCount) apply(view.getChildAt(i))
    }
}
