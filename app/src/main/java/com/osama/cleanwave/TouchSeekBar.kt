package com.osama.cleanwave

import android.content.Context
import android.view.MotionEvent
import android.widget.SeekBar

/** A gesture that starts on the slider stays on it until the finger lifts. */
internal class TouchSeekBar(context: Context) : SeekBar(context) {
    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (!isEnabled) { parent?.requestDisallowInterceptTouchEvent(false); return false }
        if (event.actionMasked == MotionEvent.ACTION_DOWN)
            parent?.requestDisallowInterceptTouchEvent(true)
        val handled = super.onTouchEvent(event)
        if (handled && event.actionMasked == MotionEvent.ACTION_UP) performClick()
        if (!handled || event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL)
            parent?.requestDisallowInterceptTouchEvent(false)
        return handled
    }
    override fun performClick(): Boolean = super.performClick()
}
