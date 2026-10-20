package com.osama.cleanwave

import android.content.Context
import android.graphics.Color
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan

internal object AudioModes {
    const val OFF = -1
    data class Mode(val id: Int, val label: Int, val description: Int, val icon: Int, val color: Int)
    val modes = listOf(
        Mode(OFF, R.string.off, R.string.mode_off_detail, R.drawable.ic_power, Color.rgb(148, 166, 192)),
        Mode(CleanerPrefs.SOURCE_MUSIC_FILTER, R.string.music_filter, R.string.mode_filter_detail, R.drawable.ic_music_filter, Color.rgb(65, 184, 127)),
        Mode(CleanerPrefs.SOURCE_OPTIMIZED, R.string.optimized, R.string.mode_optimized_detail, R.drawable.ic_sliders, Color.rgb(210, 160, 65)),
        Mode(CleanerPrefs.SOURCE_STRICT, R.string.strict, R.string.mode_strict_detail, R.drawable.ic_lock, Color.rgb(221, 113, 129)),
    )
    fun mode(id: Int) = modes.find { it.id == id } ?: modes[1]
    fun levels(context: Context, id: Int): CharSequence {
        val voice = 0
        val music = if (id == OFF) 0 else -100
        val other = when (id) { CleanerPrefs.SOURCE_STRICT -> -100; CleanerPrefs.SOURCE_OPTIMIZED -> CleanerPrefs.optimizedBackground(context); else -> 0 }
        val result = SpannableStringBuilder()
        val colors = intArrayOf(Color.rgb(65, 184, 127), Color.rgb(210, 160, 65), Color.rgb(221, 113, 129))
        listOf(R.string.voice to voice, R.string.music to music, R.string.background to other).forEachIndexed { index, (label, db) ->
            if (index > 0) result.append("   ·   ")
            val start = result.length
            result.append(context.getString(label)).append(" ").append(if (db <= -100) context.getString(R.string.muted) else "${if (db > 0) "+" else ""}$db dB")
            result.setSpan(ForegroundColorSpan(colors[index]), start, result.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        return result
    }
}
