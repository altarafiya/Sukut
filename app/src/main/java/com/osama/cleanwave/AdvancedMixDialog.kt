package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.text.InputType
import android.view.View
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Toast

internal object AdvancedMixDialog {
    fun show(activity: Activity, changed: () -> Unit): Dialog? {
        val prefs = activity.getSharedPreferences("advanced_mix_access", Context.MODE_PRIVATE)
        fun state() = AdvancedMixPolicy.State(prefs.getInt("failures", 0), prefs.getLong("locked_until", 0))
        val remaining = state().lockedUntil - System.currentTimeMillis()
        if (remaining > 0) {
            Toast.makeText(activity, ((remaining + 59_999) / 60_000).toInt().let { activity.resources.getQuantityString(R.plurals.advanced_locked, it, it) }, Toast.LENGTH_LONG).show(); return null
        }
        val ui = MobileUi(activity)
        var dialog: Dialog? = null
        val body = ui.card(20).apply { background = null; tag = "advanced_mix_dialog" }
        body.addView(ui.icon(R.drawable.ic_sliders, ui.accent, 30))
        body.addView(ui.text(R.string.advanced_mix_title, 23f, true).apply { setPadding(0, ui.dp(12), 0, ui.dp(8)); isAccessibilityHeading = true })
        body.addView(ui.text(R.string.advanced_mix_body, 15f, color = ui.secondary))
        val footer = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(ui.dp(20), 0, ui.dp(20), ui.dp(12)) }
        if (CleanerPrefs.advancedMix(activity)) {
            footer.addView(ui.button(R.string.advanced_use_standard, R.drawable.ic_sliders, MobileUi.Tone.FILLED) {
                CleanerPrefs.setAdvancedMix(activity, false); dialog?.dismiss(); changed()
            })
        } else {
            val input = EditText(activity).apply {
                tag = "advanced_mix_code"; hint = activity.getString(R.string.advanced_code_hint)
                inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
                isSingleLine = true; setTextColor(ui.primary); setHintTextColor(ui.secondary)
                textDirection = View.TEXT_DIRECTION_LTR; importantForAutofill = View.IMPORTANT_FOR_AUTOFILL_NO
                setPadding(ui.dp(12), ui.dp(12), ui.dp(12), ui.dp(12))
                background = ui.rounded(ui.cardColor, 12)
            }
            body.addView(input, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(18) })
            val error = ui.text("", 14f, color = ui.errorColor).apply { tag = "advanced_mix_error"; accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE }
            body.addView(error, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(8) })
            footer.addView(ui.button(R.string.advanced_enable, R.drawable.ic_lock, MobileUi.Tone.FILLED) {
                val attempt = AdvancedMixPolicy.attempt(state(), input.text.toString(), System.currentTimeMillis())
                prefs.edit().putInt("failures", attempt.state.failures).putLong("locked_until", attempt.state.lockedUntil).apply()
                input.text.clear()
                when (attempt.result) {
                    AdvancedMixPolicy.Result.ENABLED -> { CleanerPrefs.setAdvancedMix(activity, true); dialog?.dismiss(); changed() }
                    AdvancedMixPolicy.Result.INCORRECT -> { error.text = activity.getString(R.string.advanced_incorrect, 3 - attempt.state.failures); input.requestFocus() }
                    AdvancedMixPolicy.Result.LOCKED -> { dialog?.dismiss(); Toast.makeText(activity, activity.resources.getQuantityString(R.plurals.advanced_locked, 10, 10), Toast.LENGTH_LONG).show() }
                }
            })
        }
        footer.addView(ui.button(R.string.cancel, 0, MobileUi.Tone.PLAIN) { dialog?.dismiss() }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(8) })
        dialog = ui.sheet(body, footer)
        return dialog
    }
}
