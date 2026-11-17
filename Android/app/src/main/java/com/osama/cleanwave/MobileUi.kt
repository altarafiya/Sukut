package com.osama.cleanwave

import android.animation.ValueAnimator
import android.annotation.SuppressLint
import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.accessibility.AccessibilityNodeInfo
import android.view.animation.OvershootInterpolator
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

/**
 * Sukut's whole interface toolkit, built on framework Views only. No AppCompat,
 * Material Components or Compose runtime is loaded, which keeps start-up fast
 * and memory low. Motion happens only in response to touch; nothing animates
 * while idle.
 */
internal class MobileUi(val activity: Activity) {
    enum class Tone { FILLED, TONAL, PLAIN, WARNING }

    val palette = UiPalette(activity)
    val dark = palette.dark
    val background = palette.background
    val cardColor = palette.card
    val primary = palette.primary
    val secondary = palette.secondary
    val accent = palette.accent
    val selectedColor = palette.accentContainer
    val dividerColor = palette.divider
    val readyColor = palette.ready
    val errorColor = palette.error
    val warning = palette.warning
    val rtl = activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
    val arabic = activity.resources.configuration.locales[0].language == "ar"
    val large = activity.resources.configuration.screenWidthDp >= 840 && activity.resources.configuration.fontScale <= 1.3f
    /** Large text or a narrow window: stack paired controls instead of squeezing them. */
    val compact = activity.resources.configuration.fontScale > 1.3f || activity.resources.configuration.screenWidthDp < 360
    /** Short windows get a smaller hero so Home still fits without scrolling. */
    val short = activity.resources.configuration.screenHeightDp < 760
    val motion = ValueAnimator.areAnimatorsEnabled()

    fun dp(value: Int) = (value * activity.resources.displayMetrics.density + .5f).toInt()

    fun rounded(color: Int, radius: Int = 22) = GradientDrawable().apply {
        setColor(color); cornerRadius = dp(radius).toFloat()
    }
    fun gradient(radius: Int = 22) = rounded(palette.accent, radius)
    fun circle(color: Int) = GradientDrawable().apply { shape = GradientDrawable.OVAL; setColor(color) }
    fun surface(color: Int = cardColor, radius: Int = 22) = rounded(color, radius)
    fun pressable(color: Int = cardColor, radius: Int = 22) = RippleDrawable(
        ColorStateList.valueOf(palette.ripple), rounded(color, radius), rounded(Color.WHITE, radius))

    fun text(value: CharSequence, size: Float = 16f, bold: Boolean = false, color: Int = primary) = TextView(activity).apply {
        text = value; textSize = size; setTextColor(color); ellipsize = null
        textDirection = if (rtl) View.TEXT_DIRECTION_RTL else View.TEXT_DIRECTION_LTR
        textAlignment = View.TEXT_ALIGNMENT_GRAVITY
        gravity = if (rtl) Gravity.RIGHT else Gravity.LEFT
        typeface = Typeface.create("sans-serif", if (bold) Typeface.BOLD else Typeface.NORMAL)
        SukutTypography.apply(this)
    }
    fun text(id: Int, size: Float = 16f, bold: Boolean = false, color: Int = primary) = text(activity.getString(id), size, bold, color)

    /** Apple-style large title. */
    fun title(id: Int) = text(id, 32f, true).apply { isAccessibilityHeading = true; letterSpacing = if (arabic) 0f else -0.01f }

    fun sectionLabel(id: Int) = text(activity.getString(id).let { if (arabic) it else it.uppercase() }, 12f, true, secondary).apply {
        isAccessibilityHeading = true
        letterSpacing = if (arabic) 0f else 0.06f
        setPaddingRelative(dp(16), 0, dp(16), dp(8))
    }

    fun rule(inset: Int = 0) = View(activity).apply {
        setBackgroundColor(dividerColor)
        layoutParams = LinearLayout.LayoutParams(-1, maxOf(1, dp(1) / 2)).apply { marginStart = dp(inset) }
    }
    fun space(height: Int) = View(activity).apply { layoutParams = LinearLayout.LayoutParams(1, dp(height)) }

    fun card(padding: Int = 20) = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL; background = surface()
        setPadding(dp(padding), dp(padding), dp(padding), dp(padding))
    }
    /** A card for full-width rows: rows bring their own padding and ripple. */
    fun group() = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL; background = surface()
        clipToOutline = true
    }

    fun button(id: Int, icon: Int = 0, action: () -> Unit): TextView = button(id, icon, Tone.TONAL, action)
    fun button(id: Int, icon: Int, tone: Tone, action: () -> Unit): TextView = TextView(activity).apply {
        val ink = when (tone) {
            Tone.FILLED -> palette.onAccent
            Tone.TONAL -> palette.onAccentContainer
            Tone.PLAIN -> palette.primary
            Tone.WARNING -> palette.warning
        }
        val fill = when (tone) {
            Tone.FILLED -> gradient(18)
            Tone.TONAL -> rounded(palette.accentContainer, 18)
            Tone.PLAIN -> rounded(palette.cardAlt, 18)
            Tone.WARNING -> rounded(palette.warningContainer, 18)
        }
        text = activity.getString(id); textSize = 16f; setTextColor(ink)
        typeface = Typeface.create("sans-serif", Typeface.BOLD)
        gravity = Gravity.CENTER; textAlignment = View.TEXT_ALIGNMENT_CENTER
        minHeight = dp(52); minimumHeight = dp(52)
        setPaddingRelative(dp(20), dp(12), dp(20), dp(12))
        background = RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), fill, rounded(Color.WHITE, 18))
        isClickable = true; isFocusable = true
        if (icon != 0) {
            setCompoundDrawablesRelativeWithIntrinsicBounds(icon, 0, 0, 0)
            compoundDrawablePadding = dp(10)
            compoundDrawableTintList = ColorStateList.valueOf(ink)
        }
        accessibilityDelegate = ButtonRole
        SukutTypography.apply(this)
        springPress(this)
        setOnClickListener { tap(this); action() }
    }

    fun iconButton(icon: Int, description: Int, action: () -> Unit) = ImageView(activity).apply {
        setImageResource(icon); imageTintList = ColorStateList.valueOf(primary)
        background = pressable(cardColor, 24); contentDescription = activity.getString(description)
        setPadding(dp(11), dp(11), dp(11), dp(11))
        isClickable = true; isFocusable = true
        accessibilityDelegate = ButtonRole
        springPress(this)
        setOnClickListener { tap(this); action() }
    }

    fun brandIcon(size: Int) = ImageView(activity).apply {
        tag = "brand_icon"
        // The user's chosen icon, drawn as one rounded tile (no separate frame behind it).
        setImageDrawable(AppIcon.tile(activity))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    fun questionButton(description: Int, action: () -> Unit) = text("?", 23f, true, accent).apply {
        gravity = Gravity.CENTER; contentDescription = activity.getString(description)
        background = pressable(cardColor, 24); minimumHeight = dp(48); minimumWidth = dp(48)
        isFocusable = true; accessibilityDelegate = ButtonRole
        setOnClickListener { tap(this); action() }
    }

    fun icon(resource: Int, tint: Int, size: Int = 24) = ImageView(activity).apply {
        setImageResource(resource); imageTintList = ColorStateList.valueOf(tint)
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        layoutParams = LinearLayout.LayoutParams(dp(size), dp(size))
    }

    /** Plain row glyphs, without coloured tiles or decorative backgrounds. */
    fun badge(resource: Int, filled: Boolean = true, size: Int = 30) = ImageView(activity).apply {
        setImageResource(resource)
        imageTintList = ColorStateList.valueOf(palette.secondary)
        background = null
        val pad = dp(size) / 5
        setPadding(pad, pad, pad, pad)
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        layoutParams = LinearLayout.LayoutParams(dp(size), dp(size))
    }

    /** A settings-style row: icon, title, optional value below, optional chevron. */
    fun row(icon: Int, title: CharSequence, value: TextView? = null, chevron: Boolean = true, onClick: (() -> Unit)?) =
        row(icon, LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            addView(text(title, 16f), LinearLayout.LayoutParams(-1, -2))
            value?.let { addView(it.apply { setPaddingRelative(0, dp(2), 0, 0) }, LinearLayout.LayoutParams(-1, -2)) }
        }, chevron, onClick)

    fun row(icon: Int, content: View, chevron: Boolean = true, onClick: (() -> Unit)?) =
        LinearLayout(activity).apply {
            gravity = Gravity.CENTER_VERTICAL
            minimumHeight = dp(58)
            setPaddingRelative(dp(16), dp(10), dp(14), dp(10))
            if (onClick != null) {
                background = pressable(cardColor, 0)
                isClickable = true; isFocusable = true
                accessibilityDelegate = ButtonRole
                setOnClickListener { tap(this); onClick() }
            }
            if (icon != 0) addView(badge(icon), LinearLayout.LayoutParams(dp(30), dp(30)).apply { marginEnd = dp(14) })
            addView(content, LinearLayout.LayoutParams(0, -2, 1f))
            if (chevron) addView(icon(R.drawable.ic_chevron, secondary, 18).apply {
                alpha = .7f
                (layoutParams as LinearLayout.LayoutParams).marginStart = dp(8)
            })
        }

    fun valueText(value: CharSequence, color: Int = secondary) = text(value, 14f, color = color)

    /** A tinted message block for tips, warnings and route notices. */
    fun notice(container: Int) = LinearLayout(activity).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(container, 18)
        setPadding(dp(16), dp(14), dp(16), dp(14))
    }

    fun root(maxWidth: Int = 720, topPadding: Int = 4) = AdaptiveColumn(activity, dp(maxWidth)).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(20), dp(topPadding), dp(20), dp(24))
        setOnApplyWindowInsetsListener { view, insets ->
            val bars = systemBars(insets)
            view.setPadding(dp(20) + bars[0], dp(topPadding) + bars[1], dp(20) + bars[2], dp(24) + bars[3])
            insets
        }
    }

    fun scroll(root: View): BounceScrollView = BounceScrollView(activity).apply {
        isFillViewport = true; setBackgroundColor(this@MobileUi.background)
        isVerticalScrollBarEnabled = false
        addView(FrameLayout(activity).apply {
            addView(root, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.CENTER_HORIZONTAL))
        })
    }

    /** A light bottom sheet: one framework Dialog, rounded top, bounded height. */
    fun sheet(content: View, fixedFooter: View? = null): Dialog {
        val dialog = Dialog(activity, R.style.Sukut_Sheet)
        val maxHeight = activity.resources.displayMetrics.heightPixels * 9 / 10
        val panel = SheetColumn(activity, maxHeight, dp(13)).apply {
            orientation = LinearLayout.VERTICAL
            background = GradientDrawable().apply {
                setColor(if (dark) palette.card else palette.background)
                val r = dp(30).toFloat()
                cornerRadii = floatArrayOf(r, r, r, r, 0f, 0f, 0f, 0f)
            }
            addView(View(activity).apply {
                background = rounded(palette.secondary, 3); alpha = .35f
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }, LinearLayout.LayoutParams(dp(38), dp(5)).apply { gravity = Gravity.CENTER_HORIZONTAL; topMargin = dp(8) })
            scroll = BounceScrollView(activity).apply {
                isVerticalScrollBarEnabled = false; addView(content)
            }
            addView(scroll, LinearLayout.LayoutParams(-1, -2))
            if (fixedFooter != null) {
                val actions = FrameLayout(activity).apply {
                    tag = "sheet_footer"
                    setPadding(dp(20), dp(12), dp(20), dp(20))
                    addView(fixedFooter, FrameLayout.LayoutParams(-1, -2))
                }
                this.footer = actions
                addView(actions, LinearLayout.LayoutParams(-1, -2))
            }
            setOnApplyWindowInsetsListener { view, insets ->
                view.setPadding(0, 0, 0, systemBars(insets)[3]); insets
            }
        }
        SukutTypography.apply(content)
        fixedFooter?.let { SukutTypography.apply(it) }
        dialog.setContentView(panel)
        dialog.setCanceledOnTouchOutside(true)
        dialog.window?.apply {
            setGravity(Gravity.BOTTOM)
            setLayout(minOf(activity.resources.displayMetrics.widthPixels, dp(600)), ViewGroup.LayoutParams.WRAP_CONTENT)
            setWindowAnimations(if (motion) R.style.Sukut_SheetMotion else 0)
            navigationBarColor = if (dark) palette.card else palette.background
        }
        dialog.show()
        dialog.window?.setLayout(minOf(activity.resources.displayMetrics.widthPixels, dp(600)), ViewGroup.LayoutParams.WRAP_CONTENT)
        return dialog
    }

    /** Scale down slightly while pressed and spring back on release. */
    @SuppressLint("ClickableViewAccessibility")
    fun springPress(view: View, depth: Float = .97f) {
        if (!motion) return
        view.setOnTouchListener { target, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> target.animate().scaleX(depth).scaleY(depth).setDuration(90)
                    .setInterpolator(null).start()
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> target.animate().scaleX(1f).scaleY(1f)
                    .setDuration(120).setInterpolator(android.view.animation.DecelerateInterpolator()).start()
            }
            false
        }
    }

    /** Light tick for selections and buttons; a firmer one for on/off. */
    fun tap(view: View, strong: Boolean = false) {
        val feedback = when {
            strong && Build.VERSION.SDK_INT >= 30 -> HapticFeedbackConstants.CONFIRM
            strong -> HapticFeedbackConstants.LONG_PRESS
            else -> HapticFeedbackConstants.CLOCK_TICK
        }
        view.performHapticFeedback(feedback)
    }

    fun styleBars() {
        activity.window.statusBarColor = background; activity.window.navigationBarColor = background
        if (Build.VERSION.SDK_INT >= 30) activity.window.decorView.post {
            val mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
            activity.window.insetsController?.setSystemBarsAppearance(if (dark) 0 else mask, mask)
        } else {
            @Suppress("DEPRECATION")
            activity.window.decorView.systemUiVisibility = if (dark) 0
                else View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR or View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR
        }
    }

    companion object {
        /** left, top, right, bottom of system bars and display cutout. */
        fun systemBars(insets: WindowInsets): IntArray = if (Build.VERSION.SDK_INT >= 30) {
            val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
            intArrayOf(bars.left, bars.top, bars.right, bars.bottom)
        } else {
            @Suppress("DEPRECATION")
            intArrayOf(insets.systemWindowInsetLeft, insets.systemWindowInsetTop, insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
        }
    }
}

/** Announces a clickable view as a button to TalkBack. */
internal object ButtonRole : View.AccessibilityDelegate() {
    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(host, info); info.className = "android.widget.Button"
    }
}

internal class AdaptiveColumn(context: Context, private val maximumWidth: Int) : LinearLayout(context) {
    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val width = minOf(MeasureSpec.getSize(widthMeasureSpec), maximumWidth)
        super.onMeasure(MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY), heightMeasureSpec)
    }
}
