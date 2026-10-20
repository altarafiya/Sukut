package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.graphics.BitmapFactory
import android.graphics.Color
import android.view.Gravity
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** Both supported setup paths. Pictures are decoded only when expanded. */
internal object ShizukuGuide {
    fun add(activity: Activity, parent: LinearLayout) {
        val ui = MobileUi(activity)
        data class Method(val title: Int, val body: Int, val icon: Int, val color: Int, val wireless: Boolean)
        val methods = listOf(
            Method(R.string.guide_pc_title, R.string.guide_pc_body, R.drawable.ic_setup_pc, Color.rgb(52, 130, 255),
                false),
            Method(R.string.guide_wireless_title, R.string.guide_wireless_body, R.drawable.ic_setup_wireless, Color.rgb(31, 138, 91),
                true)
        )
        val buttons = mutableListOf<View>()
        val panels = mutableListOf<LinearLayout>()
        val arrows = mutableListOf<ImageView>()
        fun style(index: Int, expanded: Boolean) {
            val method = methods[index]
            val shape = ui.rounded(if (expanded) darken(method.color) else method.color, 14)
            buttons[index].background = android.graphics.drawable.RippleDrawable(
                android.content.res.ColorStateList.valueOf(0x40FFFFFF), shape, ui.rounded(Color.WHITE, 14))
            buttons[index].isSelected = expanded
            buttons[index].contentDescription = activity.getString(method.title) + ", " + activity.getString(if (expanded) R.string.guide_hide_steps else R.string.guide_show_steps)
            arrows[index].animate().cancel()
            if (android.animation.ValueAnimator.areAnimatorsEnabled())
                arrows[index].animate().rotation(if (expanded) 180f else 0f).setDuration(180).start()
            else arrows[index].rotation = if (expanded) 180f else 0f
        }
        methods.forEachIndexed { index, method ->
            val content = LinearLayout(activity).apply {
                orientation = LinearLayout.VERTICAL; visibility = View.GONE; tag = "shizuku_method_content_$index"
                setPadding(ui.dp(6), ui.dp(14), ui.dp(6), ui.dp(10))
            }
            val button = ui.button(method.title, method.icon) {
                val open = content.visibility != View.VISIBLE
                if (android.animation.ValueAnimator.areAnimatorsEnabled() && parent.isLaidOut)
                    android.transition.TransitionManager.beginDelayedTransition(parent,
                        android.transition.AutoTransition().apply { duration = 180 })
                panels.forEachIndexed { i, panel -> panel.visibility = View.GONE; panel.removeAllViews(); style(i, false) }
                if (open) {
                    val supported = !method.wireless || android.os.Build.VERSION.SDK_INT >= 30
                    val steps = activity.getString(if (supported) method.body else R.string.guide_wireless_min_api).split('\n')
                    steps.forEachIndexed { step, text ->
                        val block = LinearLayout(activity).apply {
                            orientation = LinearLayout.VERTICAL; tag = "shizuku_step_${index}_$step"
                        }
                        val row = LinearLayout(activity).apply { gravity = Gravity.TOP }
                        row.addView(ui.text("${step + 1}", 14f, true, method.color).apply {
                            gravity = Gravity.CENTER; background = ui.rounded(if (ui.dark) 0x223482FF else 0x113482FF, 8)
                        }, LinearLayout.LayoutParams(ui.dp(30), ui.dp(32)).apply { marginEnd = ui.dp(10) })
                        row.addView(ui.text(TextAccents.color(activity, text), 14f, color = ui.secondary), LinearLayout.LayoutParams(0, -2, 1f))
                        block.addView(row)
                        fun action(label: Int, icon: Int, click: () -> Unit) {
                            block.addView(ui.space(8)); block.addView(ui.button(label, icon, click))
                        }
                        fun picture(caption: Int, filename: String) {
                            val imageHost = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
                            action(caption, R.drawable.ic_expand_more) {
                                if (imageHost.visibility == View.VISIBLE) { imageHost.visibility = View.GONE; imageHost.removeAllViews() }
                                else {
                                    imageHost.addView(ImageView(activity).apply {
                                        adjustViewBounds = true; scaleType = ImageView.ScaleType.FIT_CENTER
                                        contentDescription = activity.getString(caption)
                                        setImageBitmap(decode(activity, filename, 480))
                                        setOnClickListener { showPicture(activity, caption, filename) }
                                    }, LinearLayout.LayoutParams(-1, ui.dp(250)))
                                    imageHost.visibility = View.VISIBLE; StartupMotion.enter(imageHost)
                                }
                            }
                            block.addView(imageHost)
                        }
                        if (method.wireless && supported) when (step) {
                            0 -> action(R.string.guide_google_play, R.drawable.ic_system_theme) { AudioTools.openShizukuStore(activity) }
                            1 -> action(R.string.guide_about_phone, R.drawable.ic_settings) { AudioTools.openAboutPhone(activity) }
                            2 -> {
                                action(R.string.guide_developer_options, R.drawable.ic_settings) { AudioTools.openWirelessDebugging(activity) }
                                picture(R.string.guide_wireless_picture, "phone-wireless-debugging.png")
                            }
                            3 -> {
                                action(R.string.open_shizuku, R.drawable.ic_system_theme) { AudioTools.openShizuku(activity) }
                                picture(R.string.guide_shizuku_picture, "phone-shizuku.png")
                            }
                            4 -> action(R.string.open_shizuku, R.drawable.ic_system_theme) { AudioTools.openShizuku(activity) }
                        }
                        content.addView(block, LinearLayout.LayoutParams(-1, -2).apply { if (step > 0) topMargin = ui.dp(14) })
                        StartupMotion.enter(block, (step * 45).toLong())
                    }
                    content.visibility = View.VISIBLE; style(index, true); SukutTypography.apply(content)
                }
            }.apply {
                minimumHeight = ui.dp(60); tag = "shizuku_method_$index"; elevation = ui.dp(2).toFloat()
                accessibilityDelegate = object : View.AccessibilityDelegate() {
                    override fun onInitializeAccessibilityNodeInfo(host: View, info: android.view.accessibility.AccessibilityNodeInfo) {
                        super.onInitializeAccessibilityNodeInfo(host, info); info.className = "android.widget.Button"
                    }
                }
            }
            (button as android.view.ViewGroup).let { group ->
                (group.getChildAt(0) as TextView).apply { setTextColor(Color.WHITE); textSize = 15f }
                (group.getChildAt(1) as ImageView).imageTintList = android.content.res.ColorStateList.valueOf(Color.WHITE)
                val arrow = ImageView(activity).apply {
                    setImageResource(R.drawable.ic_expand_more); imageTintList = android.content.res.ColorStateList.valueOf(Color.WHITE)
                    importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
                }
                group.addView(arrow, android.widget.FrameLayout.LayoutParams(ui.dp(20), ui.dp(20), Gravity.END or Gravity.CENTER_VERTICAL).apply { marginEnd = ui.dp(8) })
                arrows.add(arrow)
            }
            buttons.add(button); panels.add(content); style(index, false)
            parent.addView(button, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
            parent.addView(content)
        }
        parent.addView(ui.space(12))
        parent.addView(ui.text(TextAccents.color(activity, activity.getString(R.string.guide_restart_note)),
            13f, color = ui.warning))
    }

    private fun darken(color: Int): Int = Color.rgb(
        Color.red(color) * 8 / 10, Color.green(color) * 8 / 10, Color.blue(color) * 8 / 10)

    private fun decode(activity: Activity, name: String, width: Int): android.graphics.Bitmap? {
        val options = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        activity.assets.open("setup/$name").use { BitmapFactory.decodeStream(it, null, options) }
        options.inSampleSize = 1
        while (options.outWidth / (options.inSampleSize * 2) >= width) options.inSampleSize *= 2
        options.inJustDecodeBounds = false
        return activity.assets.open("setup/$name").use { BitmapFactory.decodeStream(it, null, options) }
    }
    private fun showPicture(activity: Activity, caption: Int, name: String) {
        val ui = MobileUi(activity); val dialog = Dialog(activity)
        val content = ui.card(12)
        content.addView(ui.text(caption, 15f, true))
        content.addView(ImageView(activity).apply { adjustViewBounds = true; setImageBitmap(decode(activity, name, 900)); contentDescription = activity.getString(caption) })
        content.addView(ui.button(R.string.performance_done) { dialog.dismiss() })
        dialog.setContentView(ScrollView(activity).apply { addView(content) }); dialog.show()
        dialog.window?.setLayout(minOf(activity.resources.displayMetrics.widthPixels - ui.dp(24), ui.dp(600)), -2)
    }
}
