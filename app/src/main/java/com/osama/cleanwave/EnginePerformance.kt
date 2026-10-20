package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.SharedPreferences
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.view.Window
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.*

internal object EnginePerformance {
    data class Mode(val label: Int, val detail: Int, val icon: Int)
    val modes = listOf(
        Mode(R.string.performance_lowest, R.string.performance_lowest_detail, R.drawable.ic_fast_output),
        Mode(R.string.performance_balanced, R.string.performance_balanced_detail, R.drawable.ic_buffer),
        Mode(R.string.output_higher_quality, R.string.performance_quality_detail, R.drawable.ic_quality),
        Mode(R.string.performance_custom, R.string.performance_custom_detail, R.drawable.ic_sliders),
    )
    fun selected(context: Context) = EnginePerformanceConfig.mode(CleanerPrefs.effectsCompatibility(context), CleanerPrefs.buffering(context), CleanerPrefs.customPerformance(context))
    fun select(context: Context, mode: Int) {
        require(mode in modes.indices)
        if (mode == EnginePerformanceConfig.CUSTOM)
            CleanerPrefs.setEnginePerformance(context, CleanerPrefs.effectsCompatibility(context), CleanerPrefs.buffering(context), custom = true)
        else EnginePerformanceConfig.presets[mode].let { CleanerPrefs.setEnginePerformance(context, it.effects, it.buffering) }
    }
    fun increaseBuffering(context: Context): Boolean {
        val next = CleanerPrefs.buffering(context) + 1
        if (next > 2) return false
        val effects = CleanerPrefs.effectsCompatibility(context)
        CleanerPrefs.setEnginePerformance(context, effects, next,
            CleanerPrefs.customPerformance(context) || EnginePerformanceConfig.mode(effects, next) == EnginePerformanceConfig.CUSTOM)
        return true
    }
    fun choices(activity: Activity, onChanged: () -> Unit = {}): View {
        val ui = MobileUi(activity)
        val panel = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; tag = "engine_performance_choices" }
        fun refresh() {
            panel.removeAllViews()
            if (CleanerPrefs.bluetoothOutput(activity)) {
                panel.addView(ui.text(TextAccents.color(activity, activity.getString(R.string.bluetooth_output_notice)), 13f, color = ui.warning).apply {
                    tag = "bluetooth_output_notice"; setPadding(0, 0, 0, ui.dp(12))
                })
            }
            modes.forEachIndexed { index, mode ->
                val checked = selected(activity) == index
                val color = if (checked) ui.accent else ui.secondary
                val shape = ui.rounded(if (checked) { if (ui.dark) 0xFF1A3051.toInt() else 0xFFE8F1FF.toInt() }
                    else if (ui.dark) 0xFF27272C.toInt() else 0xFFF6F6F9.toInt(), 16).apply { setStroke(ui.dp(1), if (checked) ui.accent else Color.TRANSPARENT) }
                val row = LinearLayout(activity).apply {
                    gravity = Gravity.CENTER_VERTICAL; minimumHeight = ui.dp(64)
                    setPadding(ui.dp(14), ui.dp(10), ui.dp(14), ui.dp(10))
                    background = RippleDrawable(ColorStateList.valueOf(0x233482FF), shape, ui.rounded(Color.WHITE, 16))
                    isClickable = true; isFocusable = true; isSelected = checked; tag = "performance_$index"
                    contentDescription = "${activity.getString(mode.label)}, ${activity.getString(mode.detail)}"
                    accessibilityDelegate = object : View.AccessibilityDelegate() {
                        override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                            super.onInitializeAccessibilityNodeInfo(host, info); info.className = "android.widget.RadioButton"
                            info.isCheckable = true; info.isChecked = checked
                        }
                    }
                    setOnClickListener { select(activity, index); refresh(); onChanged() }
                }
                row.addView(ImageView(activity).apply { setImageResource(mode.icon); imageTintList = ColorStateList.valueOf(color); importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO },
                    LinearLayout.LayoutParams(ui.dp(24), ui.dp(24)).apply { marginEnd = ui.dp(12) })
                row.addView(LinearLayout(activity).apply {
                    orientation = LinearLayout.VERTICAL; importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
                    addView(ui.text(mode.label, 15f, true, if (checked) ui.accent else ui.primary))
                    addView(ui.text(TextAccents.color(activity, activity.getString(mode.detail)), 12f, color = ui.secondary).apply { setPadding(0, ui.dp(3), 0, 0) })
                }, LinearLayout.LayoutParams(0, -2, 1f))
                row.addView(ImageView(activity).apply { setImageResource(if (checked) R.drawable.ic_check_circle else R.drawable.ic_circle_outline); imageTintList = ColorStateList.valueOf(color); importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }, LinearLayout.LayoutParams(ui.dp(20), ui.dp(20)))
                panel.addView(row, LinearLayout.LayoutParams(-1, -2).apply { if (index > 0) topMargin = ui.dp(8) })
            }
            if (selected(activity) == EnginePerformanceConfig.CUSTOM) {
                panel.addView(PerformanceSlider(activity, R.string.output_quality,
                    listOf(R.string.output_raw_quality, R.string.output_higher_quality),
                    listOf(R.drawable.ic_fast_output, R.drawable.ic_quality),
                    if (CleanerPrefs.effectsCompatibility(activity)) 1 else 0, ui.accent, "custom_output_quality") { which ->
                    CleanerPrefs.setEnginePerformance(activity, which == 1, CleanerPrefs.buffering(activity), custom = true); onChanged()
                }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
                panel.addView(PerformanceSlider(activity, R.string.buffering,
                    listOf(R.string.buffer_lowest, R.string.buffer_balanced, R.string.buffer_maximum),
                    List(3) { R.drawable.ic_buffer }, CleanerPrefs.buffering(activity), ui.warning, "custom_audio_buffering") { which ->
                    CleanerPrefs.setEnginePerformance(activity, CleanerPrefs.effectsCompatibility(activity), which, custom = true); onChanged()
                }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            }
            SukutTypography.apply(panel)
        }
        // Route changes refresh an open dialog/setup without rebuilding sliders on every drag.
        val routeChanges = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == CleanerPrefs.BLUETOOTH_OUTPUT_KEY) panel.post { if (panel.isAttachedToWindow) refresh() }
        }
        panel.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(view: View) { CleanerPrefs.registerListener(activity, routeChanges); refresh() }
            override fun onViewDetachedFromWindow(view: View) { CleanerPrefs.unregisterListener(activity, routeChanges) }
        })
        refresh()
        return panel
    }
    fun show(activity: Activity, onChanged: () -> Unit = {}): Dialog {
        val ui = MobileUi(activity)
        val dialog = Dialog(activity).apply { requestWindowFeature(Window.FEATURE_NO_TITLE) }
        val content = ui.card(20).apply {
            addView(ui.text(R.string.engine_performance, 21f, true))
            addView(ui.text(TextAccents.color(activity, activity.getString(R.string.performance_help)), 13f, color = ui.secondary).apply { setPadding(0, ui.dp(8), 0, ui.dp(16)) })
            addView(choices(activity, onChanged))
            addView(ui.space(12)); addView(ui.button(R.string.performance_done) { dialog.dismiss() })
        }
        SukutTypography.apply(content)
        dialog.setContentView(ScrollView(activity).apply { overScrollMode = View.OVER_SCROLL_NEVER; addView(content) })
        dialog.window?.setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
        dialog.show()
        dialog.window?.setLayout(minOf(activity.resources.displayMetrics.widthPixels - ui.dp(32), ui.dp(600)), -2)
        return dialog
    }
}
