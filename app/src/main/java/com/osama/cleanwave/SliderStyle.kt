package com.osama.cleanwave

import android.content.res.ColorStateList
import android.widget.SeekBar

internal object SliderStyle {
    fun apply(bar: SeekBar, ui: MobileUi, color: Int = ui.accent) = with(bar) {
        minimumHeight = ui.dp(64); minHeight = ui.dp(6); maxHeight = ui.dp(6)
        progressTintList = ColorStateList.valueOf(color)
        progressBackgroundTintList = ColorStateList.valueOf(ui.dividerColor)
        thumb = ui.circle(color).apply { setSize(ui.dp(26), ui.dp(26)); setStroke(ui.dp(3), ui.cardColor) }
        thumbTintList = null
    }
}
