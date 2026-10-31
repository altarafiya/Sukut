package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout

/** The same mode guidance is used by setup and the Home help sheet. */
internal object AudioHelp {
    private fun section(activity: Activity, icon: Int, title: Int, color: Int, points: List<Int>): View {
        val ui = MobileUi(activity)
        return LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            background = ui.rounded(ui.palette.cardAlt, 16)
            setPadding(ui.dp(16), ui.dp(14), ui.dp(16), ui.dp(14))
            addView(LinearLayout(activity).apply {
                gravity = Gravity.CENTER_VERTICAL
                addView(ui.icon(icon, color, 23), LinearLayout.LayoutParams(ui.dp(23), ui.dp(23)).apply { marginEnd = ui.dp(12) })
                addView(ui.text(title, 17f, true, color).apply { isAccessibilityHeading = true }, LinearLayout.LayoutParams(0, -2, 1f))
            })
            for (point in points) addView(LinearLayout(activity).apply {
                gravity = Gravity.TOP
                addView(ui.text("•", 17f, color = color), LinearLayout.LayoutParams(ui.dp(18), -2))
                addView(ui.text(point, 14f, color = ui.secondary).apply { setLineSpacing(ui.dp(2).toFloat(), 1f) }, LinearLayout.LayoutParams(0, -2, 1f))
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(9) })
        }
    }
    fun modeCards(activity: Activity): View {
        val ui = MobileUi(activity)
        return LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            val cards = listOf(
                section(activity, R.drawable.ic_music_filter, R.string.music_filter, ModeStyle.audio(CleanerPrefs.SOURCE_MUSIC_FILTER, ui.dark), listOf(R.string.help_filter_1, R.string.help_filter_2, R.string.help_filter_misses)),
                section(activity, R.drawable.ic_sliders, R.string.optimized, ModeStyle.audio(CleanerPrefs.SOURCE_OPTIMIZED, ui.dark), listOf(R.string.help_optimized_1, R.string.help_optimized_2)),
                section(activity, R.drawable.ic_lock, R.string.strict, ModeStyle.audio(CleanerPrefs.SOURCE_STRICT, ui.dark), listOf(R.string.help_strict_1, R.string.help_strict_2)))
            cards.forEachIndexed { i, card -> addView(card, LinearLayout.LayoutParams(-1, -2).apply { if (i > 0) topMargin = ui.dp(12) }) }
            addView(section(activity, R.drawable.ic_notifications, R.string.help_notifications_title, ui.warning,
                listOf(R.string.help_notifications_1, R.string.help_notifications_2)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
        }
    }
    private fun sheet(activity: Activity, title: Int, body: View): Dialog {
        val ui = MobileUi(activity)
        var dialog: Dialog? = null
        val panel = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(ui.dp(20), ui.dp(18), ui.dp(20), ui.dp(20))
            addView(ui.text(title, 25f, true).apply { isAccessibilityHeading = true })
            addView(body, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(18) })

        }
        val done = ui.button(R.string.performance_done, 0, MobileUi.Tone.FILLED) { dialog?.dismiss() }.apply { tag = "help_done" }
        return ui.sheet(panel, done).also { dialog = it }
    }
    fun showModes(activity: Activity): Dialog {
        val ui = MobileUi(activity)
        val body = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            addView(modeCards(activity))
            addView(section(activity, R.drawable.ic_power, R.string.help_start_title, ui.accent, listOf(R.string.help_start_1)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
        }
        return sheet(activity, R.string.audio_mode, body)
    }
    fun showPerformance(activity: Activity): Dialog {
        val ui = MobileUi(activity)
        val body = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            tag = "performance_help_sections"
            addView(section(activity, R.drawable.ic_headphones, R.string.phone_effects, ui.accent, listOf(R.string.help_effects_1, R.string.help_effects_2, R.string.help_effects_3)))
            addView(section(activity, ModeStyle.bufferIcon(CleanerPrefs.buffering(activity)), R.string.buffering, ModeStyle.buffer(CleanerPrefs.buffering(activity), ui.dark), listOf(R.string.help_buffer_intro)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            listOf(R.string.buffer_lowest to R.string.help_buffer_lowest, R.string.buffer_balanced to R.string.help_buffer_balanced, R.string.buffer_maximum to R.string.help_buffer_maximum).forEachIndexed { index, text ->
                addView(section(activity, ModeStyle.bufferIcon(index), text.first, ModeStyle.buffer(index, ui.dark), listOf(text.second)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            }
            addView(section(activity, R.drawable.ic_delay, R.string.help_delay_title, ui.accent, listOf(R.string.help_delay_1, R.string.help_delay_2, R.string.help_delay_3)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            addView(section(activity, R.drawable.ic_info, R.string.help_good_to_know, ui.accent, listOf(R.string.help_same_filter)), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
        }
        return sheet(activity, R.string.engine_performance, body)
    }
}
