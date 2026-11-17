package com.osama.cleanwave

import android.animation.ValueAnimator
import android.content.Context
import android.widget.ScrollView

/** Native edge stretch on Android 12+, edge glow on older supported phones.
 * No content translation, gesture cancellation or synthetic scroll-to-top.
 * Child controls can own a drag with requestDisallowInterceptTouchEvent.
 */
internal class BounceScrollView(context: Context, private var maximumHeight: Int = 0) : ScrollView(context) {
    internal fun limitHeight(value: Int) { if (maximumHeight != value) { maximumHeight = value; forceLayout() } }
    init {
        overScrollMode = if (ValueAnimator.areAnimatorsEnabled()) OVER_SCROLL_ALWAYS else OVER_SCROLL_NEVER
        val accent = UiPalette(context).accent
        topEdgeEffectColor = accent; bottomEdgeEffectColor = accent
    }
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val bounded = if (maximumHeight > 0) {
            val height = if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.UNSPECIFIED) maximumHeight
                else minOf(maximumHeight, MeasureSpec.getSize(heightMeasureSpec))
            MeasureSpec.makeMeasureSpec(height, MeasureSpec.AT_MOST)
        } else heightMeasureSpec
        super.onMeasure(widthMeasureSpec, bounded)
    }
}
