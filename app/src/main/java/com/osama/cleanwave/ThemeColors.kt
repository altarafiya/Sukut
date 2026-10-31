package com.osama.cleanwave

import kotlin.math.pow

/** Opaque theme colours; independent of Android so custom colours can be tested. */
internal object ThemeColors {
    data class Surfaces(val background: Int, val card: Int, val inset: Int, val divider: Int)
    fun mix(base: Int, tint: Int, amount: Float): Int {
        val fraction = amount.coerceIn(0f, 1f)
        var rgb = 0xFF000000.toInt()
        for (shift in intArrayOf(16, 8, 0)) {
            val a = base ushr shift and 255; val b = tint ushr shift and 255
            rgb = rgb or ((a + (b - a) * fraction + .5f).toInt() shl shift)
        }
        return rgb
    }
    fun surfaces(dark: Boolean, tinted: Boolean, color: Int): Surfaces {
        if (!tinted) return if (dark) Surfaces(0xFF000000.toInt(), 0xFF1C1C1C.toInt(), 0xFF2C2C2C.toInt(), 0xFF383838.toInt())
            else Surfaces(0xFFFFFFFF.toInt(), 0xFFF4F4F4.toInt(), 0xFFE8E8E8.toInt(), 0xFFDDDDDD.toInt())
        return if (dark) Surfaces(mix(0xFF121212.toInt(), color, .06f), mix(0xFF1C1C1C.toInt(), color, .16f), mix(0xFF2C2C2C.toInt(), color, .16f), mix(0xFF383838.toInt(), color, .18f))
            else Surfaces(mix(0xFFFFFFFF.toInt(), color, .05f), mix(0xFFFFFFFF.toInt(), color, .025f), mix(0xFFE8E8E8.toInt(), color, .10f), mix(0xFFDDDDDD.toInt(), color, .15f))
    }
    private fun luminance(color: Int): Double {
        fun channel(shift: Int): Double { val v = (color ushr shift and 255) / 255.0; return if (v <= .04045) v / 12.92 else ((v + .055) / 1.055).pow(2.4) }
        return .2126 * channel(16) + .7152 * channel(8) + .0722 * channel(0)
    }
    fun contrast(a: Int, b: Int): Double { val x = luminance(a); val y = luminance(b); return (maxOf(x, y) + .05) / (minOf(x, y) + .05) }
    fun readableAccent(color: Int, background: Int, dark: Boolean): Int {
        val end = if (dark) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
        for (step in 0..20) { val candidate = mix(color, end, step / 20f); if (contrast(candidate, background) >= 4.5) return candidate }
        return end
    }
    fun onAccent(color: Int): Int = if (contrast(color, 0xFFFFFFFF.toInt()) >= contrast(color, 0xFF000000.toInt())) 0xFFFFFFFF.toInt() else 0xFF000000.toInt()
    fun hex(color: Int): String = "#%06X".format(color and 0xFFFFFF)
    fun parseHex(value: String): Int? = value.trim().removePrefix("#").takeIf { it.matches(Regex("[0-9a-fA-F]{6}")) }?.toInt(16)?.or(0xFF000000.toInt())
}
