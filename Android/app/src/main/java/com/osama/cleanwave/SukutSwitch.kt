package com.osama.cleanwave

import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.InsetDrawable
import android.graphics.drawable.StateListDrawable
import android.widget.Switch

/** Apple-like pill and white thumb, with native drag, keyboard and TalkBack. */
internal class SukutSwitch(context: Context) : Switch(context) {
    private var touchStart = false
    private var clicked = false
    init {
        val palette = UiPalette(context)
        fun dp(value: Int) = (value * resources.displayMetrics.density + .5f).toInt()
        fun track(on: Boolean) = GradientDrawable().apply {
            setSize(dp(52), dp(31)); cornerRadius = dp(16).toFloat()
            val blue = ThemeColors.readableAccent(palette.accent, 0xFFFFFFFF.toInt(), false)
            setColor(if (on) blue else if (palette.dark) 0xFF494C52.toInt() else 0xFF989DA5.toInt())
        }
        fun thumb(): InsetDrawable {
            val base = GradientDrawable().apply {
                shape = GradientDrawable.OVAL; setSize(dp(25), dp(25)); setColor(0xFFFFFFFF.toInt())
                setStroke(dp(1), 0x18000000)
            }
            return InsetDrawable(base, dp(3))
        }
        trackDrawable = StateListDrawable().apply { addState(intArrayOf(android.R.attr.state_checked), track(true)); addState(intArrayOf(), track(false)) }
        thumbDrawable = thumb()
        trackTintList = null; thumbTintList = null
        showText = false; splitTrack = false; switchMinWidth = dp(52)
        minimumWidth = dp(52); minimumHeight = dp(48); switchPadding = 0
        setPadding(0, dp(9), 0, dp(9))
    }
    override fun toggle() {
        super.toggle()
        clicked = true
        if (isEnabled && isAttachedToWindow) performHapticFeedback(android.view.HapticFeedbackConstants.CLOCK_TICK)
    }
    override fun onTouchEvent(event: android.view.MotionEvent): Boolean {
        if (event.actionMasked == android.view.MotionEvent.ACTION_DOWN) { touchStart = isChecked; clicked = false }
        val handled = super.onTouchEvent(event)
        if (event.actionMasked == android.view.MotionEvent.ACTION_UP && !clicked && touchStart != isChecked && isEnabled)
            performHapticFeedback(android.view.HapticFeedbackConstants.CLOCK_TICK)
        return handled
    }
}
