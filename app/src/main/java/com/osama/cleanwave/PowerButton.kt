package com.osama.cleanwave

import android.animation.ArgbEvaluator
import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo

/** Flat power control. No glow, shine, bitmap, shader or idle animation. */
internal class PowerButton(context: Context, private val palette: UiPalette, private val sizeDp: Int = 136) : View(context) {
    enum class State { OFF, STARTING, ON, STANDBY, ATTENTION }
    var state = State.OFF
        private set
    private val density = resources.displayMetrics.density
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val ring = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = density }
    private val glyph = context.getDrawable(R.drawable.ic_power)!!.mutate()
    private val sleepGlyph=context.getDrawable(R.drawable.ic_sleep)!!.mutate()
    private class Look(val base: Int, val ring: Int, val glyph: Int)
    private var look = target(State.OFF)
    private var animator: ValueAnimator? = null
    private var modeColor = palette.accent
    fun setModeColor(color: Int) {
        if (modeColor == color) return
        modeColor = color; animator?.cancel(); look = target(state); apply(look)
    }
    init { isClickable = true; isFocusable = true; apply(look) }

    fun show(next: State, description: CharSequence) {
        contentDescription = description
        if (next == state) return
        state = next
        val from = look; val to = target(next)
        animator?.cancel()
        if (!ValueAnimator.areAnimatorsEnabled() || !isAttachedToWindow) { look = to; apply(to); return }
        val colors = ArgbEvaluator()
        animator = ValueAnimator.ofFloat(0f, 1f).apply {
            duration = 150
            addUpdateListener {
                val f = it.animatedFraction
                fun mix(a: Int, b: Int) = colors.evaluate(f, a, b) as Int
                look = Look(mix(from.base, to.base), mix(from.ring, to.ring), mix(from.glyph, to.glyph))
                apply(look)
            }
            start()
        }
    }
    private fun target(state: State) = when (state) {
        State.OFF -> Look(palette.cardAlt, palette.divider, palette.secondary)
        State.ON -> Look(modeColor, 0, ThemeColors.onAccent(modeColor))
        State.STARTING -> Look(ThemeColors.mix(palette.card, modeColor, .18f), modeColor, modeColor)
        State.STANDBY -> Look(modeColor,0,ThemeColors.onAccent(modeColor))
        State.ATTENTION -> Look(palette.warningContainer, palette.warning, palette.warning)
    }
    private fun apply(value: Look) { fill.color = value.base; ring.color = value.ring; glyph.setTint(value.glyph); sleepGlyph.setTint(value.glyph); invalidate() }
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val size = (sizeDp * density).toInt()
        setMeasuredDimension(resolveSize(size, widthMeasureSpec), resolveSize(size, heightMeasureSpec))
    }
    override fun onDraw(canvas: Canvas) {
        val cx = width / 2f; val cy = height / 2f; val radius = minOf(width, height) * .39f
        canvas.drawCircle(cx, cy, radius, fill)
        if (ring.color != 0) canvas.drawCircle(cx, cy, radius - density / 2, ring)
        val g = radius * .44f
        val symbol=if(state==State.STANDBY)sleepGlyph else glyph
        symbol.setBounds((cx-g).toInt(), (cy-g).toInt(), (cx+g).toInt(), (cy+g).toInt()); symbol.draw(canvas)
    }
    override fun onDetachedFromWindow() { animator?.cancel(); animator = null; super.onDetachedFromWindow() }
    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        info.className = "android.widget.Switch"; info.isCheckable = true
        info.isChecked = state == State.ON || state == State.STANDBY || state == State.STARTING
    }
}
