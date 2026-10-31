package com.osama.cleanwave

import android.content.Context

/** Black/white by default. Optional tint affects surfaces and readable accents. */
internal class UiPalette(context: Context) {
    val dark = UiPrefs.isDark(context)
    private val tinted = UiPrefs.tinted(context)
    private val tint = UiPrefs.tintColor(context)
    private val surfaces = ThemeColors.surfaces(dark, tinted, tint)
    val background = surfaces.background
    val card = surfaces.card
    val cardAlt = surfaces.inset
    val primary = if (dark) 0xFFFFFFFF.toInt() else 0xFF1C1C1C.toInt()
    val secondary = ThemeColors.readableAccent(if (dark) 0xFFA6A6A6.toInt() else 0xFF626262.toInt(), cardAlt, dark)
    val accent = ThemeColors.readableAccent(if (dark) 0xFF80BFEA.toInt() else 0xFF17659B.toInt(), cardAlt, dark)
    val gradientStart = accent
    val gradientEnd = accent
    val onAccent = ThemeColors.onAccent(accent)
    val accentContainer = ThemeColors.mix(card, accent, if (dark) .20f else .10f)
    val onAccentContainer = ThemeColors.readableAccent(accent, accentContainer, dark)
    val highlight = accent
    val divider = surfaces.divider
    val ready = accent
    val warning = ThemeColors.readableAccent(if (dark) 0xFFE3C184.toInt() else 0xFF805B20.toInt(), cardAlt, dark)
    val warningContainer = if (dark) 0xFF3B3324.toInt() else 0xFFF4ECD9.toInt()
    val error = ThemeColors.readableAccent(if (dark) 0xFFE9A3A0.toInt() else 0xFFA53C39.toInt(), cardAlt, dark)
    val errorContainer = if (dark) 0xFF402A28.toInt() else 0xFFF7E4E0.toInt()
    val ripple = accent and 0xFFFFFF or 0x26000000
}
