package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.provider.Settings
import android.content.res.ColorStateList
import android.widget.LinearLayout

/** One shared guide; only the selected activation method is built/shown. */
internal object WorkerGuide {
    const val PHONE = 0
    const val WINDOWS = 1
    fun add(activity: Activity, parent: LinearLayout, pair: (() -> Unit)? = null, methodChanged: (Int) -> Unit = {}) {
        parent.addView(create(activity, UiPrefs.activationMethod(activity),
            null,
            pair ?: { AppNavigation.openActivation(activity, PHONE, LocalActivationService.PAIR) }, methodChanged))
    }
    fun create(activity: Activity, initial: Int, home: (() -> Unit)?, pair: () -> Unit, methodChanged: (Int)->Unit = {}): LinearLayout {
        val ui = MobileUi(activity)
        val root = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; tag = "activation_methods" }
        val body = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL }
        var selected = if (Build.VERSION.SDK_INT < 30) WINDOWS else initial.coerceIn(PHONE,WINDOWS)
        fun step(title: Int, detail: Int, icon:Int=R.drawable.ic_settings, color:Int=ui.accent, content: LinearLayout.() -> Unit = {}) {
            body.addView(ui.card(16).apply {
                addView(LinearLayout(activity).apply {
                    gravity=android.view.Gravity.CENTER_VERTICAL
                    addView(ui.icon(icon,color,22),LinearLayout.LayoutParams(ui.dp(22),ui.dp(22)).apply { marginEnd=ui.dp(10) })
                    addView(ui.text(title,17f,true,color).apply { isAccessibilityHeading=true },LinearLayout.LayoutParams(0,-2,1f))
                })
                addView(ui.text(TextAccents.color(activity,activity.getString(detail)), 14f, color = ui.secondary).apply { setPadding(0, ui.dp(8), 0, ui.dp(12)); setLineSpacing(ui.dp(2).toFloat(),1f) })
                content()
                for(index in 2 until childCount) {
                    getChildAt(index).layoutParams=LinearLayout.LayoutParams(-1,-2).apply {
                        if(index>2)topMargin=ui.dp(12)
                    }
                }
            }, LinearLayout.LayoutParams(-1,-2).apply { topMargin = ui.dp(if(body.childCount==0)12 else 18) })
        }
        fun render() {
            body.removeAllViews(); body.tag = if (selected == PHONE) "guide_phone" else "guide_windows"
            if(selected==PHONE)body.addView(ui.button(R.string.guide_watch_video,R.drawable.ic_tutorial_video,MobileUi.Tone.PLAIN) {
                AudioTools.website(activity,ReleaseLinks.TUTORIAL_VIDEO)
            }.apply {
                tag="guide_watch_video"
            },LinearLayout.LayoutParams(-1,-2).apply { topMargin=ui.dp(12) })
            step(R.string.guide_developer_1,R.string.guide_developer_body,R.drawable.ic_settings) {
                addView(ui.button(R.string.guide_about_phone,R.drawable.ic_phone) { AudioTools.openAboutPhone(activity) })
            }
            if (selected == PHONE) {
                if (Build.VERSION.SDK_INT < 30) { body.addView(ui.text(R.string.local_android10, 14f)); return }
                step(R.string.guide_phone_1, R.string.guide_phone_1_body,R.drawable.ic_setup_wireless) {
                    addView(GuidePictures.button(activity, GuidePictures.Picture.PAIRING))
                    addView(GuidePictures.button(activity, GuidePictures.Picture.NOTIFICATION))
                }
                step(R.string.guide_phone_2, R.string.guide_phone_2_body,R.drawable.ic_pair_key,ui.warning) {
                    addView(ui.button(R.string.local_pair_button, R.drawable.ic_pair_key, MobileUi.Tone.FILLED, pair).apply { tag="guide_pair_phone" })
                    addView(GuidePictures.button(activity, GuidePictures.Picture.WIRELESS))
                }
                step(R.string.guide_phone_3, R.string.guide_phone_3_body,R.drawable.ic_check_circle,ModeStyle.buffer(0,ui.dark)) {
                    home?.let { addView(ui.button(R.string.guide_open_home, R.drawable.ic_sukut_tile, MobileUi.Tone.TONAL, it)) }
                }
                body.addView(ui.text(R.string.guide_after_activation, 13f, color = ui.secondary).apply { setPadding(0,ui.dp(12),0,0) })
            } else {
                step(R.string.guide_windows_1, R.string.guide_windows_1_body,R.drawable.ic_phone) {
                    addView(ui.button(R.string.guide_developer_options, R.drawable.ic_settings) {
                        runCatching { activity.startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)) }
                    })
                    addView(GuidePictures.button(activity, GuidePictures.Picture.DEVELOPER))
                }
                step(R.string.guide_windows_2, R.string.guide_windows_2_body,R.drawable.ic_phone)
                step(R.string.guide_windows_3, R.string.guide_windows_3_body,R.drawable.ic_setup_pc,ModeStyle.buffer(0,ui.dark))
                body.addView(ui.text(R.string.guide_after_activation, 13f, color = ui.secondary).apply { setPadding(0,ui.dp(12),0,0) })
            }
            SukutTypography.apply(body)
        }
        root.addView(SegmentedChoice(activity, listOf(activity.getString(R.string.guide_method_phone), activity.getString(R.string.guide_method_windows)), selected,icons=listOf(R.drawable.ic_phone,R.drawable.ic_setup_pc)) {
            selected = it; UiPrefs.setActivationMethod(activity,it); methodChanged(it); render()
        }.apply { tag = "activation_method_choice" })
        root.addView(body); render(); return root
    }
}
