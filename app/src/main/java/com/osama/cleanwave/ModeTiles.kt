package com.osama.cleanwave

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * The three filtering modes as side-by-side tiles. Compact enough that Home
 * fits one screen; the selected mode's explanation is shown once below them.
 */
internal class ModeTiles(
    activity: Activity,
    modes: List<AudioModes.Mode>,
    private val onSelect: (Int) -> Unit,
) : LinearLayout(activity) {
    private val ui = MobileUi(activity)
    private class Tile(val id: Int, val view: LinearLayout, val fill: GradientDrawable, val icon: ImageView, val label: TextView)
    private val tiles = mutableListOf<Tile>()
    private var selectedId = Int.MIN_VALUE

    init {
        isBaselineAligned = false
        orientation = if (ui.compact) VERTICAL else HORIZONTAL
        modes.forEachIndexed { index, mode ->
            val fill = ui.rounded(ui.cardColor, 18)
            val icon = ImageView(activity).apply {
                setImageResource(mode.icon); importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
            }
            val label = ui.text(mode.label, 14f, true).apply {
                minLines = 2; maxLines = 2; includeFontPadding = true
                gravity = Gravity.CENTER; textAlignment = View.TEXT_ALIGNMENT_CENTER
                importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
            }
            val view = LinearLayout(activity).apply {
                orientation = if (ui.compact) HORIZONTAL else VERTICAL
                gravity = Gravity.CENTER
                tag = "mode_${mode.id}"
                minimumHeight = ui.dp(if (ui.compact) 56 else 84)
                setPadding(ui.dp(8), ui.dp(12), ui.dp(8), ui.dp(12))
                background = fill
                isClickable = true; isFocusable = true
                contentDescription = activity.getString(mode.label) + ", " + activity.getString(mode.description)
                accessibilityDelegate = object : View.AccessibilityDelegate() {
                    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                        super.onInitializeAccessibilityNodeInfo(host, info)
                        info.className = "android.widget.RadioButton"; info.isCheckable = true; info.isChecked = host.isSelected
                    }
                }
                addView(icon, LayoutParams(ui.dp(26), ui.dp(26)).apply {
                    if (ui.compact) marginEnd = ui.dp(12) else bottomMargin = ui.dp(6)
                })
                addView(label, LayoutParams(if (ui.compact) 0 else -1, -2, if (ui.compact) 1f else 0f))
                ui.springPress(this, .95f)
                setOnClickListener { if (selectedId != mode.id) ui.tap(this); onSelect(mode.id) }
            }
            tiles.add(Tile(mode.id, view, fill, icon, label))
            addView(view, if (ui.compact) LayoutParams(-1, -2).apply { if (index > 0) topMargin = ui.dp(8) }
                else LayoutParams(0, -1, 1f).apply { if (index > 0) marginStart = ui.dp(8) })
        }
    }

    fun select(id: Int, available: Boolean = true) {
        selectedId = id
        tiles.forEach { tile ->
            val on = tile.id == id
            tile.view.isSelected = on; tile.view.isEnabled = available; tile.view.alpha = if (available) 1f else .5f
            val color = ui.accent
            val surface = ThemeColors.mix(ui.cardColor, color, if (ui.dark) .18f else .09f)
            tile.fill.setColor(if (on) surface else ui.cardColor)
            tile.fill.setStroke(ui.dp(2), if (on) color else ui.cardColor)
            val ink = ThemeColors.readableAccent(color, if (on) surface else ui.cardColor, ui.dark)
            tile.icon.imageTintList = ColorStateList.valueOf(if (on) ink else ui.secondary)
            tile.label.setTextColor(if (on) ink else ui.primary)
        }
    }
}
