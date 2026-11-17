package com.osama.cleanwave

import android.content.res.ColorStateList
import android.graphics.drawable.ClipDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.view.Gravity
import android.widget.SeekBar

internal object SliderStyle {
    fun apply(bar: SeekBar, ui: MobileUi, color: Int = ui.accent) = with(bar) {
        minimumHeight = ui.dp(64); minHeight = ui.dp(6); maxHeight = ui.dp(6)
        progressTintList = ColorStateList.valueOf(color)
        progressBackgroundTintList = ColorStateList.valueOf(ui.dividerColor)
        thumb = ui.circle(color).apply { setSize(ui.dp(26), ui.dp(26)); setStroke(ui.dp(3), ui.cardColor) }
        thumbTintList = null
    }

    /** Home's mix sliders: a thick rounded track that runs almost edge to edge, and a larger thumb. */
    @android.annotation.SuppressLint("RtlHardcoded") // SeekBar mirrors itself; see below
    fun wide(bar: SeekBar, ui: MobileUi, color: Int = ui.accent) = with(bar) {
        val radius = ui.dp(6).toFloat()
        val background = GradientDrawable().apply { cornerRadius = radius; setColor(ThemeColors.mix(ui.cardColor, color, .18f)) }
        val filled = ClipDrawable(GradientDrawable().apply { cornerRadius = radius; setColor(color) }, Gravity.LEFT, ClipDrawable.HORIZONTAL)
        // LEFT, not START: SeekBar mirrors its own drawing in Arabic.
        progressDrawable = LayerDrawable(arrayOf(background, filled)).apply {
            setId(0, android.R.id.background); setId(1, android.R.id.progress)
        }
        progressTintList = null; progressBackgroundTintList = null
        minimumHeight = ui.dp(56); minHeight = ui.dp(12); maxHeight = ui.dp(12)
        thumb = ui.circle(color).apply { setSize(ui.dp(30), ui.dp(30)); setStroke(ui.dp(4), ui.cardColor) }
        thumbTintList = null; splitTrack = false; tickMark = null
        // Padding of half the thumb keeps it inside the card at both ends.
        setPaddingRelative(ui.dp(15), 0, ui.dp(15), 0)
    }
}
