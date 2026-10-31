package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.view.View
import android.widget.LinearLayout

/** Reconnection never clears onboarding or sends an existing user through it again. */
internal object WorkerReconnect {
    fun show(activity: Activity): Dialog {
        val ui = MobileUi(activity)
        var dialog: Dialog? = null
        val body = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL; tag = "worker_reconnect"
            setPadding(ui.dp(20), ui.dp(16), ui.dp(20), ui.dp(20))
            addView(ui.text(R.string.reconnect_worker, 22f, true).apply { isAccessibilityHeading = true })
            addView(ui.text(TextAccents.color(activity, activity.getString(R.string.reconnect_explanation)), 15f, color = ui.secondary).apply {
                setPadding(0, ui.dp(8), 0, ui.dp(18))
            })
            val guide = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
            addView(ui.button(R.string.local_title, R.drawable.ic_setup_wireless, MobileUi.Tone.FILLED) {
                dialog?.dismiss()
                activity.startActivity(android.content.Intent(activity, LocalActivationActivity::class.java))
            })
            addView(ui.space(8))
            addView(ui.button(R.string.reconnect_help, R.drawable.ic_setup_wireless) {
                // Built on first open only; most reconnects never need it.
                if (guide.childCount == 0) { WorkerGuide.add(activity, guide); SukutTypography.apply(guide) }
                guide.visibility = if (guide.visibility == View.VISIBLE) View.GONE else View.VISIBLE
            })
            addView(guide)
            addView(ui.space(8))
            addView(ui.button(R.string.cancel, 0, MobileUi.Tone.PLAIN) { dialog?.dismiss() })
        }
        return ui.sheet(body).also { dialog = it }
    }
}
