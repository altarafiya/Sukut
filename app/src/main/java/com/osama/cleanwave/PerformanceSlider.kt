package com.osama.cleanwave

import android.app.Activity
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.ClipDrawable
import android.graphics.drawable.LayerDrawable
import android.os.Build
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar

/** Discrete engine options: preview while dragging, configure once on release. */
internal class PerformanceSlider(
    activity: Activity,
    title: Int,
    labels: List<Int>,
    icons: List<Int>,
    selected: Int,
    color: Int,
    sliderTag: String,
    onSelect: (Int) -> Unit,
) : LinearLayout(activity) {
    init {
        val ui = MobileUi(activity)
        val ink = if (ui.dark) color else Color.rgb((Color.red(color) * .66f).toInt(),
            (Color.green(color) * .66f).toInt(), (Color.blue(color) * .66f).toInt())
        orientation = VERTICAL
        setPadding(ui.dp(14), ui.dp(14), ui.dp(14), ui.dp(12))
        background = ui.rounded(if (ui.dark) Color.rgb(39, 39, 44) else Color.rgb(246, 246, 249), 18)
        val icon = ImageView(activity).apply {
            imageTintList = ColorStateList.valueOf(color)
            background = ui.rounded((color and 0x00FFFFFF) or 0x22000000, 10)
            setPadding(ui.dp(7), ui.dp(7), ui.dp(7), ui.dp(7))
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        addView(LinearLayout(activity).apply {
            gravity = Gravity.CENTER_VERTICAL
            addView(icon, LayoutParams(ui.dp(36), ui.dp(36)).apply { marginEnd = ui.dp(10) })
            addView(ui.text(title, 15f, true), LayoutParams(0, -2, 1f))
        }, LayoutParams(-1, -2))
        val value = ui.text(labels[selected], 16f, true, ink).apply {
            setPadding(0, ui.dp(10), 0, 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        addView(value, LayoutParams(-1, -2))
        val steps = labels.mapIndexed { index, label ->
            ui.text(label, 11f).apply {
                gravity = when (index) { 0 -> Gravity.START; labels.lastIndex -> Gravity.END; else -> Gravity.CENTER }
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }
        }
        var committed = selected
        var dragging = false
        var cancelled = false
        fun commit(position: Int) {
            if (position != committed) { committed = position; onSelect(position) }
        }
        val slider = SeekBar(activity).apply {
            tag = sliderTag
            max = labels.lastIndex; progress = selected; setKeyProgressIncrement(1)
            minimumHeight = ui.dp(64); minHeight = ui.dp(22); maxHeight = ui.dp(22)
            setPadding(ui.dp(18), 0, ui.dp(18), 0)
            splitTrack = false; tickMark = null
            val track = ui.rounded(if (ui.dark) Color.rgb(65, 65, 73) else Color.rgb(214, 216, 224), 11)
            val fill = ClipDrawable(ui.rounded(color, 11), Gravity.LEFT, ClipDrawable.HORIZONTAL)
            progressDrawable = LayerDrawable(arrayOf(track, fill)).apply {
                setId(0, android.R.id.background); setId(1, android.R.id.progress)
            }
            // Retain the distinct blue/amber colors in light and dark themes.
            progressTintList = null; progressBackgroundTintList = null
            thumb = ui.circle(color).apply {
                setSize(ui.dp(36), ui.dp(36)); setStroke(ui.dp(3), if (ui.dark) Color.rgb(239, 241, 247) else Color.WHITE)
            }
            thumbTintList = null
            setOnTouchListener { _, event ->
                if (event.actionMasked == MotionEvent.ACTION_DOWN) cancelled = false
                if (event.actionMasked == MotionEvent.ACTION_CANCEL) cancelled = true
                false
            }
        }
        fun preview(position: Int) {
            value.setText(labels[position]); icon.setImageResource(icons[position])
            steps.forEachIndexed { index, label -> label.setTextColor(if (index == position) ink else ui.secondary) }
            slider.contentDescription = activity.getString(title) + ", " + activity.getString(labels[position])
            if (Build.VERSION.SDK_INT >= 30) slider.stateDescription = activity.getString(labels[position])
        }
        slider.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(seekBar: SeekBar) { dragging = true }
            override fun onProgressChanged(seekBar: SeekBar, position: Int, fromUser: Boolean) {
                preview(position)
                // Keyboard and accessibility edits are already completed actions.
                if (fromUser && !dragging) commit(position)
            }
            override fun onStopTrackingTouch(seekBar: SeekBar) {
                dragging = false
                if (cancelled) { seekBar.progress = committed; preview(committed) }
                else commit(seekBar.progress)
            }
        })
        preview(selected)
        addView(slider, LayoutParams(-1, ui.dp(64)))
        addView(LinearLayout(activity).apply {
            gravity = Gravity.TOP
            steps.forEach { addView(it, LayoutParams(0, -2, 1f)) }
        }, LayoutParams(-1, -2))
    }
}
