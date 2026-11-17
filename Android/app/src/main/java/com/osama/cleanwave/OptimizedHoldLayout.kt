package com.osama.cleanwave

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.widget.LinearLayout

/** One pending UI callback during a stationary press; none while idle or hidden. */
internal class OptimizedHoldLayout(context: Context, private val action: () -> Unit) : LinearLayout(context) {
    private val handler = Handler(Looper.getMainLooper())
    private val slop = ViewConfiguration.get(context).scaledTouchSlop
    private var x = 0f
    private var y = 0f
    private var fired = false
    private val held = Runnable { if (isAttachedToWindow && isShown && isEnabled && hasWindowFocus()) { fired = true; performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS); action() } }
    private fun cancel() { handler.removeCallbacks(held) }
    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> { cancel(); fired = false; x = event.x; y = event.y; handler.postDelayed(held, AdvancedMixPolicy.holdMillis(CleanerPrefs.advancedMix(context))) }
            MotionEvent.ACTION_MOVE -> if (kotlin.math.abs(event.x - x) > slop || kotlin.math.abs(event.y - y) > slop) cancel()
            MotionEvent.ACTION_POINTER_DOWN, MotionEvent.ACTION_CANCEL -> cancel()
            MotionEvent.ACTION_UP -> {
                cancel()
                if (fired) {
                    val cancelled = MotionEvent.obtain(event).apply { action = MotionEvent.ACTION_CANCEL }
                    super.dispatchTouchEvent(cancelled); cancelled.recycle(); fired = false; return true
                }
            }
        }
        return super.dispatchTouchEvent(event)
    }
    override fun onKeyDown(keyCode: Int, event: KeyEvent): Boolean {
        if (keyCode in intArrayOf(KeyEvent.KEYCODE_ENTER, KeyEvent.KEYCODE_SPACE, KeyEvent.KEYCODE_DPAD_CENTER) && event.repeatCount == 0) {
            cancel(); fired = false; handler.postDelayed(held, AdvancedMixPolicy.holdMillis(CleanerPrefs.advancedMix(context)))
        }
        return super.onKeyDown(keyCode, event)
    }
    override fun onKeyUp(keyCode: Int, event: KeyEvent): Boolean {
        cancel(); if (fired) { fired = false; isPressed = false; return true }
        return super.onKeyUp(keyCode, event)
    }
    override fun onWindowFocusChanged(hasWindowFocus: Boolean) { if (!hasWindowFocus) cancel(); super.onWindowFocusChanged(hasWindowFocus) }
    override fun onWindowVisibilityChanged(visibility: Int) { if (visibility != View.VISIBLE) cancel(); super.onWindowVisibilityChanged(visibility) }
    override fun onDetachedFromWindow() { cancel(); super.onDetachedFromWindow() }
}
