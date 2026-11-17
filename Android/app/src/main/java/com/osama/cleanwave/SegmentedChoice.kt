package com.osama.cleanwave

import android.app.Activity
import android.view.Gravity
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.LinearLayout
import android.widget.TextView

/**
 * An iOS-style segmented control: a grey track with one raised white (or
 * graphite) segment. Stacks vertically at large text sizes.
 */
internal class SegmentedChoice(
    activity: Activity,
    labels: List<CharSequence>,
    selected: Int,
    private val colors: List<Int>? = null,
    icons: List<Int>? = null,
    private val onSelect: (Int) -> Unit,
) : LinearLayout(activity) {
    private val ui = MobileUi(activity)
    private val segments = mutableListOf<TextView>()
    private var painted = false
    var selected = selected
        private set

    init {
        isBaselineAligned = false
        orientation = if (ui.compact) VERTICAL else HORIZONTAL
        background = ui.rounded(ui.palette.cardAlt, 12)
        setPadding(ui.dp(3), ui.dp(3), ui.dp(3), ui.dp(3))
        labels.forEachIndexed { index, label ->
            val segment = ui.text(label, 14f, true).apply {
                gravity = Gravity.CENTER; textAlignment = View.TEXT_ALIGNMENT_CENTER
                minHeight = ui.dp(48); maxLines = if (ui.compact) 2 else 1; includeFontPadding = true
                setPadding(ui.dp(6), ui.dp(8), ui.dp(6), ui.dp(8))
                icons?.getOrNull(index)?.let { resource ->
                    val glyph=activity.getDrawable(resource)!!.mutate().apply { setBounds(0,0,ui.dp(18),ui.dp(18)) }
                    setCompoundDrawablesRelative(glyph,null,null,null); compoundDrawablePadding=ui.dp(7)
                }
                isClickable = true; isFocusable = true
                accessibilityDelegate = object : View.AccessibilityDelegate() {
                    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                        super.onInitializeAccessibilityNodeInfo(host, info)
                        info.className = "android.widget.RadioButton"; info.isCheckable = true; info.isChecked = host.isSelected
                    }
                }
                setOnClickListener {
                    if (this@SegmentedChoice.selected != index) { ui.tap(this); select(index); onSelect(index) }
                }
            }
            segments.add(segment)
            addView(segment, if (orientation == HORIZONTAL) LayoutParams(0, -1, 1f) else LayoutParams(-1, -2))
        }
        select(selected)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = MeasureSpec.getSize(widthMeasureSpec)
        if (width > 0 && segments.isNotEmpty()) {
            val cell = (width - paddingLeft - paddingRight).toFloat() / segments.size
            val fits = segments.all { it.paint.measureText(it.text.toString()) + it.compoundPaddingLeft + it.compoundPaddingRight <= cell }
            val next = if (ui.compact || !fits) VERTICAL else HORIZONTAL
            if (orientation != next) {
                orientation = next
                for (segment in segments) segment.layoutParams = if (next == HORIZONTAL) LayoutParams(0, -1, 1f) else LayoutParams(-1, -2)
            }
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec)
    }

    fun select(index: Int) {
        if (painted && selected == index) return
        selected = index
        segments.forEachIndexed { i, segment ->
            val on = i == index
            segment.isSelected = on
            val color = colors?.getOrNull(i) ?: ui.accent
            val surface = ThemeColors.mix(ui.palette.cardAlt, color, if (ui.dark) .18f else .09f)
            segment.background = if (on) ui.rounded(surface, 9).apply { setStroke(ui.dp(1), color) } else ui.pressable(ui.palette.cardAlt, 9)
            val ink=if(on)ThemeColors.readableAccent(color,surface,ui.dark)else ui.secondary
            segment.setTextColor(ink); segment.compoundDrawablesRelative.forEach { it?.setTint(ink) }
            segment.elevation = 0f
            segment.translationZ = 0f
        }
        painted = true
    }
}
