package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** Shared native cards; retains the existing mobile visual language. */
internal class MobileUi(val activity: Activity) {
    private val palette = UiPalette(activity)
    val dark = palette.dark
    val background = palette.background
    val cardColor = palette.card
    val primary = palette.primary
    val secondary = palette.secondary
    val accent = Color.rgb(52, 130, 255)
    val warning = Color.rgb(233, 155, 38)
    val large = activity.resources.configuration.screenWidthDp >= 720
    fun dp(value: Int) = (value * activity.resources.displayMetrics.density + .5f).toInt()
    fun rounded(color: Int, radius: Int = 22) = GradientDrawable().apply {
        setColor(color); cornerRadius = dp(radius).toFloat()
    }
    fun circle(color: Int) = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(color) }
    fun text(value: CharSequence, size: Float = 15f, bold: Boolean = false, color: Int = primary) = TextView(activity).apply {
        text = value; textSize = size; setTextColor(color)
        textDirection = if (activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL) View.TEXT_DIRECTION_RTL else View.TEXT_DIRECTION_LTR
        textAlignment = View.TEXT_ALIGNMENT_GRAVITY
        gravity = if (activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL) Gravity.RIGHT else Gravity.LEFT
        typeface = Typeface.create("sans-serif", if (bold) Typeface.BOLD else Typeface.NORMAL)
        SukutTypography.apply(this)
    }
    fun text(id: Int, size: Float = 15f, bold: Boolean = false, color: Int = primary) = text(activity.getString(id), size, bold, color)
    fun space(height: Int) = View(activity).apply { layoutParams = LinearLayout.LayoutParams(1, dp(height)) }
    fun card(padding: Int = 18) = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL; background = rounded(cardColor)
        setPadding(dp(padding), dp(padding), dp(padding), dp(padding))
    }
    fun button(id: Int, icon: Int = 0, action: () -> Unit): View = FrameLayout(activity).apply {
        minimumHeight = dp(48); setPadding(dp(12), dp(10), dp(12), dp(10))
        background = RippleDrawable(ColorStateList.valueOf(Color.argb(35, 52, 130, 255)),
            rounded(if (dark) Color.rgb(39, 42, 49) else Color.rgb(234, 241, 255), 14), rounded(Color.WHITE, 14))
        isClickable = true; isFocusable = true; contentDescription = activity.getString(id)
        addView(text(id, 14f, true, accent).apply {
            gravity = Gravity.CENTER
            if (icon != 0) setPadding(dp(32), 0, dp(32), 0)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER))
        if (icon != 0) addView(ImageView(activity).apply {
            setImageResource(icon); imageTintList = ColorStateList.valueOf(accent)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, FrameLayout.LayoutParams(dp(22), dp(22), Gravity.START or Gravity.CENTER_VERTICAL))
        setOnClickListener { action() }
    }
    fun root(maxWidth: Int = 760, topPadding: Int = 8) = AdaptiveColumn(activity, dp(maxWidth)).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(22), dp(topPadding), dp(22), dp(30))
        setOnApplyWindowInsetsListener { view, insets ->
            val left: Int; val right: Int; val top: Int; val bottom: Int
            if (Build.VERSION.SDK_INT >= 30) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                left = bars.left; right = bars.right; top = bars.top; bottom = bars.bottom
            } else {
                @Suppress("DEPRECATION")
                left = insets.systemWindowInsetLeft
                @Suppress("DEPRECATION")
                right = insets.systemWindowInsetRight
                @Suppress("DEPRECATION")
                top = insets.systemWindowInsetTop
                @Suppress("DEPRECATION")
                bottom = insets.systemWindowInsetBottom
            }
            view.setPadding(dp(22) + left, dp(topPadding) + top, dp(22) + right, dp(30) + bottom)
            insets
        }
    }
    fun scroll(root: View): ScrollView = ScrollView(activity).apply {
        isFillViewport = true; overScrollMode = View.OVER_SCROLL_NEVER; setBackgroundColor(this@MobileUi.background)
        addView(FrameLayout(activity).apply {
            addView(root, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        })
    }
    fun styleBars() {
        activity.window.statusBarColor = background; activity.window.navigationBarColor = background
        if (Build.VERSION.SDK_INT >= 30) activity.window.decorView.post {
            val mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
            activity.window.insetsController?.setSystemBarsAppearance(if (dark) 0 else mask, mask)
        }
    }
}

internal class AdaptiveColumn(context: Context, private val maximumWidth: Int) : LinearLayout(context) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = minOf(MeasureSpec.getSize(widthMeasureSpec), maximumWidth)
        super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), heightMeasureSpec)
    }
}
