package com.osama.cleanwave

import android.content.Context
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout

/** Measure the fixed actions first, then give the scroll area the remaining room. */
internal class SheetColumn(context: Context, private val maximumHeight: Int, private val handleSpace: Int) : LinearLayout(context) {
    lateinit var scroll: BounceScrollView
    var footer: View? = null
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val available = if (MeasureSpec.getMode(heightMeasureSpec) == MeasureSpec.UNSPECIFIED) maximumHeight
            else minOf(maximumHeight, MeasureSpec.getSize(heightMeasureSpec))
        footer?.measure(ViewGroup.getChildMeasureSpec(widthMeasureSpec, paddingLeft + paddingRight, ViewGroup.LayoutParams.MATCH_PARENT),
            MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED))
        scroll.limitHeight((available - paddingTop - paddingBottom - handleSpace - (footer?.measuredHeight ?: 0)).coerceAtLeast(1))
        super.onMeasure(widthMeasureSpec, MeasureSpec.makeMeasureSpec(available, MeasureSpec.AT_MOST))
    }
}
