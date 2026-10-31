package com.osama.cleanwave

/** All audio modes share blue selection; buffering keeps its three accents. */
internal object ModeStyle {
    @Suppress("UNUSED_PARAMETER")
    fun audio(mode: Int, dark: Boolean): Int = if (dark) 0xFF80BFEA.toInt() else 0xFF17659B.toInt()
    fun buffer(mode: Int, dark: Boolean): Int = when (mode) {
        0 -> if (dark) 0xFF80C5A7.toInt() else 0xFF246B52.toInt()
        1 -> if (dark) 0xFFE7C48E.toInt() else 0xFF795315.toInt()
        else -> if (dark) 0xFFF2A7AB.toInt() else 0xFFA02C3B.toInt()
    }
    fun bufferIcon(mode: Int) = when (mode) { 0 -> R.drawable.ic_fast_output; 1 -> R.drawable.ic_buffer; else -> R.drawable.ic_delay }
}
