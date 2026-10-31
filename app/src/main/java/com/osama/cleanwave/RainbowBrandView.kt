package com.osama.cleanwave

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Rect
import android.graphics.Shader
import android.os.SystemClock
import android.view.Gravity
import android.view.ViewTreeObserver
import android.widget.TextView

/** One cached rainbow shader. No sparkles, particles or loop off screen. */
internal class RainbowBrandView(context: Context) : TextView(context) {
    private val colors = intArrayOf(0xFFFF919C.toInt(), 0xFFFFCF83.toInt(), 0xFF8EE0B4.toInt(), 0xFF80C9FA.toInt(), 0xFFB9A4FF.toInt(), 0xFFFF919C.toInt()).let { colors ->
        val palette = UiPalette(context)
        colors.map { ThemeColors.readableAccent(it, palette.card, palette.dark) }.toIntArray()
    }
    private val matrix = Matrix()
    private val visibleRect = Rect()
    private var shader: LinearGradient? = null
    private var running = false
    private val frame = object : Runnable { override fun run() { if (running) { invalidate(); postDelayed(this, 33) } } }
    private val scrollListener = ViewTreeObserver.OnScrollChangedListener { updateMotion() }
    private val layoutListener = ViewTreeObserver.OnGlobalLayoutListener { updateMotion() }
    init {
        text = "الطرفية"; textSize = 25f; gravity = Gravity.CENTER
        textAlignment = TEXT_ALIGNMENT_CENTER; setTextColor(colors[3])
        setPadding(0, 8, 0, 8); tag = "rainbow_developer"
        SukutTypography.apply(this)
    }
    private fun updateMotion() {
        val next = isAttachedToWindow && isShown && hasWindowFocus() && getGlobalVisibleRect(visibleRect) && ValueAnimator.areAnimatorsEnabled()
        if (next == running) return
        running = next; removeCallbacks(frame)
        if (next) post(frame) else invalidate()
    }
    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        viewTreeObserver.addOnScrollChangedListener(scrollListener)
        viewTreeObserver.addOnGlobalLayoutListener(layoutListener)
        updateMotion()
    }
    override fun onDetachedFromWindow() {
        running = false; removeCallbacks(frame)
        if (viewTreeObserver.isAlive) { viewTreeObserver.removeOnScrollChangedListener(scrollListener); viewTreeObserver.removeOnGlobalLayoutListener(layoutListener) }
        super.onDetachedFromWindow()
    }
    override fun onWindowFocusChanged(hasWindowFocus: Boolean) { super.onWindowFocusChanged(hasWindowFocus); updateMotion() }
    override fun onWindowVisibilityChanged(visibility: Int) { super.onWindowVisibilityChanged(visibility); if (isAttachedToWindow) updateMotion() }
    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        shader = LinearGradient(0f, 0f, maxOf(1, w).toFloat(), 0f, colors, null, Shader.TileMode.REPEAT)
        paint.shader = shader
    }
    override fun onDraw(canvas: Canvas) {
        val phase = if (running) (SystemClock.uptimeMillis() % 7000L) / 7000f else 0f
        matrix.setTranslate(phase * width, 0f); shader?.setLocalMatrix(matrix)
        super.onDraw(canvas)
    }
}
