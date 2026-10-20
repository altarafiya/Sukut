package com.osama.cleanwave

import android.content.Context
import android.graphics.Color

/** Shared surfaces that follow the selected System, Light or Dark appearance. */
internal class UiPalette(context: Context) {
    val dark = UiPrefs.isDark(context)
    val background = if (dark) Color.rgb(13, 13, 15) else Color.rgb(246, 246, 248)
    val card = if (dark) Color.rgb(29, 29, 32) else Color.WHITE
    val primary = if (dark) Color.rgb(247, 247, 247) else Color.rgb(20, 20, 22)
    val secondary = if (dark) Color.rgb(190, 190, 196) else Color.rgb(100, 100, 107)
}
