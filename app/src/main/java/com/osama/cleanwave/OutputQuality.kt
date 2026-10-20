package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.RippleDrawable
import android.view.Gravity
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.ImageView
import android.widget.LinearLayout

/** User-facing names for the existing effects/fast renderer; AI quality is identical. */
internal object OutputQuality {
    data class Option(val label: Int, val detail: Int, val icon: Int)
    val options = listOf(
        Option(R.string.output_higher_quality, R.string.output_higher_quality_hint, R.drawable.ic_quality),
        Option(R.string.output_raw_quality, R.string.output_raw_quality_hint, R.drawable.ic_fast_output),
    )

    fun selected(context: Context) = if (CleanerPrefs.effectsCompatibility(context)) 0 else 1

    fun select(context: Context, index: Int) {
        require(index in options.indices)
        if (selected(context) == index) return
        CleanerPrefs.setEffectsCompatibility(context, index == 0)
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(context)) context.startService(
            Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
    }

    fun setupChoices(activity: Activity): View {
        val ui = MobileUi(activity)
        val panel = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        fun refresh() {
            panel.removeAllViews()
            options.forEachIndexed { index, option ->
                val checked = selected(activity) == index
                val color = if (checked) ui.accent else ui.secondary
                val row = LinearLayout(activity).apply {
                    gravity = Gravity.CENTER_VERTICAL
                    minimumHeight = ui.dp(76)
                    setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(14))
                    val surface = if (checked) {
                        if (ui.dark) Color.rgb(26, 48, 81) else Color.rgb(232, 241, 255)
                    } else if (ui.dark) Color.rgb(39, 39, 44) else Color.rgb(246, 246, 249)
                    val shape = ui.rounded(surface, 16).apply { setStroke(ui.dp(1), if (checked) ui.accent else Color.TRANSPARENT) }
                    background = RippleDrawable(ColorStateList.valueOf(Color.argb(35, 52, 130, 255)), shape, ui.rounded(Color.WHITE, 16))
                    isClickable = true; isFocusable = true; isSelected = checked
                    contentDescription = "${activity.getString(option.label)}, ${activity.getString(option.detail)}"
                    accessibilityDelegate = object : View.AccessibilityDelegate() {
                        override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                            super.onInitializeAccessibilityNodeInfo(host, info)
                            info.className = "android.widget.RadioButton"
                            info.isCheckable = true; info.isChecked = checked
                        }
                    }
                    setOnClickListener { select(activity, index); refresh() }
                }
                row.addView(ImageView(activity).apply {
                    setImageResource(option.icon); imageTintList = ColorStateList.valueOf(color)
                    importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                }, LinearLayout.LayoutParams(ui.dp(28), ui.dp(28)).apply { marginEnd = ui.dp(12) })
                row.addView(LinearLayout(activity).apply {
                    orientation = LinearLayout.VERTICAL
                    importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
                    addView(ui.text(option.label, 15f, true, if (checked) ui.accent else ui.primary))
                    addView(ui.text(option.detail, 12f, color = ui.secondary).apply { setPadding(0, ui.dp(4), 0, 0) })
                }, LinearLayout.LayoutParams(0, -2, 1f))
                row.addView(ImageView(activity).apply {
                    setImageResource(if (checked) R.drawable.ic_check_circle else R.drawable.ic_circle_outline)
                    imageTintList = ColorStateList.valueOf(color)
                    importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                }, LinearLayout.LayoutParams(ui.dp(22), ui.dp(22)).apply { marginStart = ui.dp(10) })
                panel.addView(row, LinearLayout.LayoutParams(-1, -2).apply { if (index > 0) topMargin = ui.dp(8) })
            }
        }
        refresh()
        return panel
    }
}
