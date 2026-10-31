package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.graphics.BitmapFactory
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout

/** Redacted real-phone examples, decoded only while their viewer is open. */
internal object GuidePictures {
    enum class Picture(val file: String, val title: Int) {
        DEVELOPER("developer.webp", R.string.guide_picture_usb),
        WIRELESS("wireless.webp", R.string.guide_picture_wireless),
        PAIRING("pairing.webp", R.string.guide_picture_pairing),
        NOTIFICATION("notification.webp", R.string.guide_picture_notification)
    }
    fun button(activity: Activity, picture: Picture): View {
        val ui = MobileUi(activity)
        var opened: Dialog? = null
        return ui.button(picture.title, R.drawable.ic_info, MobileUi.Tone.PLAIN) {
            if(activity.isFinishing || activity.isDestroyed)return@button
            opened?.dismiss()
            val image = ImageView(activity).apply { adjustViewBounds = true; scaleType = ImageView.ScaleType.FIT_CENTER; contentDescription = activity.getString(picture.title) }
            val body = LinearLayout(activity).apply {
                orientation = LinearLayout.VERTICAL; setPadding(ui.dp(18), ui.dp(12), ui.dp(18), ui.dp(12))
                addView(ui.text(picture.title, 20f, true))
                addView(ui.text(R.string.guide_photo_note, 12f, color = ui.secondary).apply { setPadding(0, ui.dp(8), 0, ui.dp(12)) })
                addView(image, LinearLayout.LayoutParams(-1, -2))
            }
            val bitmap = activity.assets.open("setup-guide/"+picture.file).use {
                BitmapFactory.decodeStream(it, null, BitmapFactory.Options().apply { inSampleSize = 2; inScaled = false })
            }
            image.setImageBitmap(bitmap)
            opened = ui.sheet(body, ui.button(R.string.performance_done, 0, MobileUi.Tone.FILLED) { opened?.dismiss() }).also { dialog ->
                dialog.setOnDismissListener { image.setImageDrawable(null); if (opened === dialog) opened = null }
            }
        }.apply {
            tag = "guide_picture_"+picture.name.lowercase()
            addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
                override fun onViewAttachedToWindow(v: View) {}
                override fun onViewDetachedFromWindow(v: View) { opened?.dismiss(); opened = null }
            })
        }
    }
}
