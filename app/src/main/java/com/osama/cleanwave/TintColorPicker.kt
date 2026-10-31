package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.text.Editable
import android.text.TextWatcher
import android.text.InputFilter
import android.text.InputType
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.SeekBar

/** Small on-demand colour sheet: HSV field, hue, accessible hex input and preview. */
internal object TintColorPicker {
    fun show(activity: Activity, initial: Int, onSave: (Int) -> Unit): Dialog {
        val ui = MobileUi(activity)
        var dialog: Dialog? = null
        var selected = initial
        var applying = false
        val preview = ui.rounded(initial, 12).apply { setStroke(ui.dp(1), ui.dividerColor) }
        val square = ColorField(activity, initial).apply { tag = "tint_color_field" }
        val code = EditText(activity).apply {
            tag = "tint_hex"; contentDescription = activity.getString(R.string.tint_hex)
            setTextColor(ui.primary); setSingleLine(true)
            backgroundTintList = android.content.res.ColorStateList.valueOf(ui.accent)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS or InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS
            filters = arrayOf(InputFilter.LengthFilter(7)); textDirection = View.TEXT_DIRECTION_LTR
            setSelectAllOnFocus(true); setText(ThemeColors.hex(initial))
            minimumHeight = ui.dp(48)
        }
        val hue = TouchSeekBar(activity).apply {
            tag = "tint_hue"; contentDescription = activity.getString(R.string.tint_hue)
            max = 360; progress = square.hue.toInt(); layoutDirection = View.LAYOUT_DIRECTION_LTR
            minimumHeight = ui.dp(52); minHeight = ui.dp(8); maxHeight = ui.dp(8)
            val rainbow = GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT,
                intArrayOf(Color.RED, Color.YELLOW, Color.GREEN, Color.CYAN, Color.BLUE, Color.MAGENTA, Color.RED)).apply { cornerRadius = ui.dp(4).toFloat() }
            progressDrawable = LayerDrawable(arrayOf(rainbow))
            progressTintList = null; progressBackgroundTintList = null; thumbTintList = null
            thumb = ui.circle(ui.primary).apply { setSize(ui.dp(24), ui.dp(24)); setStroke(ui.dp(3), ui.cardColor) }
        }
        fun update(color: Int, updateCode: Boolean) {
            selected = color; preview.setColor(color)
            if (updateCode) { applying = true; code.setText(ThemeColors.hex(color)); applying = false }
        }
        square.onChanged = { update(it, true) }
        hue.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar?, value: Int, user: Boolean) { if (user) update(square.setHue(value.toFloat()), true) }
            override fun onStartTrackingTouch(bar: SeekBar?) {}
            override fun onStopTrackingTouch(bar: SeekBar?) {}
        })
        code.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {
                if (applying) return
                ThemeColors.parseHex(s.toString())?.let { color -> square.setColor(color); hue.progress = square.hue.toInt(); update(color, false); code.error = null }
            }
            override fun afterTextChanged(s: Editable?) {}
        })
        val body = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(ui.dp(20), ui.dp(18), ui.dp(20), ui.dp(20))
            addView(ui.text(R.string.tint_color, 24f, true).apply { isAccessibilityHeading = true })
            addView(ui.text(R.string.tint_picker_hint, 14f, color = ui.secondary), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(6); bottomMargin = ui.dp(16) })
            addView(square, LinearLayout.LayoutParams(-1, ui.dp(180)))
            addView(ui.text(R.string.tint_hue, 14f, true), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            addView(hue, LinearLayout.LayoutParams(-1, ui.dp(52)))
            addView(LinearLayout(activity).apply {
                gravity = Gravity.CENTER_VERTICAL
                addView(View(activity).apply { background = preview; importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }, LinearLayout.LayoutParams(ui.dp(48), ui.dp(48)).apply { marginEnd = ui.dp(14) })
                addView(code, LinearLayout.LayoutParams(0, -2, 1f))
            })
            addView(ui.button(R.string.tint_apply, 0, MobileUi.Tone.FILLED) {
                if (ThemeColors.parseHex(code.text.toString()) == null) { code.error = activity.getString(R.string.tint_hex_error); code.requestFocus() }
                else { dialog?.dismiss(); onSave(selected) }
            }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(18) })
            addView(ui.button(android.R.string.cancel, 0, MobileUi.Tone.PLAIN) { dialog?.dismiss() }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(8) })
        }
        return ui.sheet(body).also { dialog = it }
    }

    private class ColorField(context: Context, initial: Int) : View(context) {
        private val hsv = FloatArray(3).apply { Color.colorToHSV(initial, this) }
        private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        private val border = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
        private val bounds = RectF()
        private val clip = Path()
        private val density = resources.displayMetrics.density
        private var saturation: Shader? = null
        private var brightness: Shader? = null
        var onChanged: ((Int) -> Unit)? = null
        val hue get() = hsv[0]
        init { importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO; isClickable = true }
        fun setColor(color: Int) { Color.colorToHSV(color, hsv); shaders(); invalidate() }
        fun setHue(value: Float): Int { hsv[0] = value; shaders(); invalidate(); return Color.HSVToColor(hsv) }
        private fun shaders() {
            if (width == 0 || height == 0) return
            val pad = 10 * density
            bounds.set(pad, pad, width-pad, height-pad); clip.reset(); clip.addRoundRect(bounds, 10*density, 10*density, Path.Direction.CW)
            saturation = LinearGradient(bounds.left, 0f, bounds.right, 0f, Color.WHITE, Color.HSVToColor(floatArrayOf(hsv[0], 1f, 1f)), Shader.TileMode.CLAMP)
            brightness = LinearGradient(0f, bounds.top, 0f, bounds.bottom, Color.TRANSPARENT, Color.BLACK, Shader.TileMode.CLAMP)
        }
        override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) { shaders() }
        override fun onDraw(canvas: Canvas) {
            canvas.save(); canvas.clipPath(clip)
            paint.shader = saturation; canvas.drawRect(bounds, paint)
            paint.shader = brightness; canvas.drawRect(bounds, paint); canvas.restore()
            val x = bounds.left + hsv[1]*bounds.width(); val y = bounds.top + (1-hsv[2])*bounds.height()
            border.shader = null; border.color = Color.BLACK; border.strokeWidth = 4*density; canvas.drawCircle(x, y, 7*density, border)
            border.color = Color.WHITE; border.strokeWidth = 2*density; canvas.drawCircle(x, y, 7*density, border)
        }
        override fun onTouchEvent(event: MotionEvent): Boolean {
            if (event.actionMasked == MotionEvent.ACTION_DOWN) parent?.requestDisallowInterceptTouchEvent(true)
            if (event.actionMasked == MotionEvent.ACTION_DOWN || event.actionMasked == MotionEvent.ACTION_MOVE || event.actionMasked == MotionEvent.ACTION_UP) {
                hsv[1] = ((event.x-bounds.left)/bounds.width().coerceAtLeast(1f)).coerceIn(0f, 1f)
                hsv[2] = (1-(event.y-bounds.top)/bounds.height().coerceAtLeast(1f)).coerceIn(0f, 1f)
                invalidate(); onChanged?.invoke(Color.HSVToColor(hsv))
            }
            if (event.actionMasked == MotionEvent.ACTION_UP || event.actionMasked == MotionEvent.ACTION_CANCEL) {
                parent?.requestDisallowInterceptTouchEvent(false)
                if (event.actionMasked == MotionEvent.ACTION_UP) performClick()
            }
            return true
        }
        override fun performClick(): Boolean = super.performClick()
    }
}
