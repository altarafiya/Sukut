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
 * One accessible single choice: icon, label, optional explanation, optional
 * meter line and a check mark. Selection only recolours existing views, so it
 * never relayouts.
 */
internal class SelectionRow(
    activity: Activity, label: Int, icon: Int, detail: Int = 0, glyph: String = "",
    meter: CharSequence? = null, action: () -> Unit,
) : LinearLayout(activity) {
    private val ui = MobileUi(activity)
    private val labelView: TextView
    private val iconView: ImageView?
    private val glyphView: TextView?
    private val mark: ImageView
    private val fill: GradientDrawable
    init {
        orientation = HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
        minimumHeight = ui.dp(if (detail == 0) 58 else 72)
        setPaddingRelative(ui.dp(14), ui.dp(12), ui.dp(14), ui.dp(12))
        fill = ui.rounded(ui.cardColor, 18)
        background = fill
        isClickable = true; isFocusable = true
        contentDescription = activity.getString(label) + (if (detail == 0) "" else ", ${activity.getString(detail)}") +
            (meter?.let { ", $it" } ?: "")
        accessibilityDelegate = object : View.AccessibilityDelegate() {
            override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                super.onInitializeAccessibilityNodeInfo(host, info)
                info.className = "android.widget.RadioButton"; info.isCheckable = true; info.isChecked = host.isSelected
            }
        }
        val roomy = !(resources.configuration.fontScale > 1.5f && resources.configuration.screenWidthDp < 400)
        iconView = if (icon != 0 && roomy) ImageView(activity).apply {
            setImageResource(icon); importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
            val pad = ui.dp(7); setPadding(pad, pad, pad, pad)
            this@SelectionRow.addView(this, LayoutParams(ui.dp(36), ui.dp(36)).apply { marginEnd = ui.dp(14); gravity = Gravity.TOP })
        } else null
        glyphView = if (icon == 0 && glyph.isNotEmpty()) ui.text(glyph, 17f, true).apply {
            gravity = Gravity.CENTER; importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
            this@SelectionRow.addView(this, LayoutParams(ui.dp(36), ui.dp(36)).apply { marginEnd = ui.dp(14) })
        } else null
        val textColumn = LinearLayout(activity).apply {
            orientation = VERTICAL; importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        }
        labelView = ui.text(label, 16f, true)
        textColumn.addView(labelView, LayoutParams(-1, -2))
        if (detail != 0) textColumn.addView(ui.text(detail, 14f, color = ui.secondary).apply {
            setPaddingRelative(0, ui.dp(2), 0, 0); setLineSpacing(ui.dp(1).toFloat(), 1f)
        }, LayoutParams(-1, -2))
        if (meter != null) textColumn.addView(ui.text(meter, 12f, true, ui.secondary).apply {
            setPaddingRelative(0, ui.dp(6), 0, 0)
        }, LayoutParams(-1, -2))
        addView(textColumn, LayoutParams(0, -2, 1f))
        mark = ImageView(activity).apply { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO }
        addView(mark, LayoutParams(ui.dp(24), ui.dp(24)).apply { marginStart = ui.dp(12) })
        ui.springPress(this, .98f)
        setOnClickListener { if (!isSelected) ui.tap(this); action() }
        select(false)
    }
    fun select(checked: Boolean, available: Boolean = true) {
        isSelected = checked; isEnabled = available; alpha = if (available) 1f else .5f
        fill.setColor(if (checked) ui.selectedColor else ui.cardColor)
        fill.setStroke(ui.dp(2), if (checked) ui.accent else ui.cardColor)
        labelView.setTextColor(if (checked) ui.palette.onAccentContainer else ui.primary)
        iconView?.imageTintList = ColorStateList.valueOf(if (checked) ui.palette.onAccent else ui.palette.onAccentContainer)
        iconView?.background = if (checked) ui.gradient(10) else ui.rounded(ui.palette.accentContainer, 10)
        glyphView?.setTextColor(if (checked) ui.palette.onAccent else ui.palette.onAccentContainer)
        glyphView?.background = if (checked) ui.gradient(10) else ui.rounded(ui.palette.accentContainer, 10)
        mark.setImageResource(if (checked) R.drawable.ic_check_circle else R.drawable.ic_circle_outline)
        mark.imageTintList = ColorStateList.valueOf(if (checked) ui.accent else ui.dividerColor)
    }
}
