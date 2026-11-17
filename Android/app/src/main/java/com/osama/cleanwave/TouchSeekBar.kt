package com.osama.cleanwave

import android.content.Context
import android.view.MotionEvent
import android.view.View
import android.widget.SeekBar
import kotlin.math.roundToInt

/**
 * A slider that follows the finger anywhere on its track, not only from the thumb:
 * pressing the track jumps there at once and dragging continues from it. A gesture
 * that starts on the slider stays on it until the finger lifts. Keyboard and
 * accessibility changes go through SeekBar as before.
 */
internal class TouchSeekBar(context: Context) : SeekBar(context) {
    private var client: OnSeekBarChangeListener? = null
    private var touching = false

    override fun setOnSeekBarChangeListener(listener: OnSeekBarChangeListener?) {
        client = listener
        // Changes made by this class's own touch handling are reported below, as user changes.
        super.setOnSeekBarChangeListener(listener?.let { forward ->
            object : OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) { if (!touching) forward.onProgressChanged(bar, progress, fromUser) }
                override fun onStartTrackingTouch(bar: SeekBar?) { if (!touching) forward.onStartTrackingTouch(bar) }
                override fun onStopTrackingTouch(bar: SeekBar?) { if (!touching) forward.onStopTrackingTouch(bar) }
            }
        })
    }

    private fun progressAt(x: Float): Int {
        val track = (width - paddingLeft - paddingRight).coerceAtLeast(1)
        var fraction = ((x - paddingLeft) / track).coerceIn(0f, 1f)
        if (layoutDirection == View.LAYOUT_DIRECTION_RTL) fraction = 1f - fraction
        return min + (fraction * (max - min)).roundToInt()
    }

    private fun follow(x: Float) {
        val next = progressAt(x)
        if (next == progress) return
        progress = next
        client?.onProgressChanged(this, next, true)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled) { parent?.requestDisallowInterceptTouchEvent(false); return false }
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                touching = true; isPressed = true
                client?.onStartTrackingTouch(this)
                follow(event.x)
            }
            MotionEvent.ACTION_MOVE -> if (touching) follow(event.x)
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                if (touching) {
                    if (event.actionMasked == MotionEvent.ACTION_UP) follow(event.x)
                    isPressed = false
                    client?.onStopTrackingTouch(this)
                    touching = false
                    if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
                }
                parent?.requestDisallowInterceptTouchEvent(false)
            }
        }
        return true
    }
    override fun performClick(): Boolean = super.performClick()
}
