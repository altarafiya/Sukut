package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.widget.LinearLayout

/** Worker loss changes Home's status. Help appears only after the user's action. */
internal object WorkerReconnect {
    fun show(activity: Activity): Dialog {
        val ui=MobileUi(activity)
        var dialog:Dialog?=null
        val body=LinearLayout(activity).apply {
            orientation=LinearLayout.VERTICAL;tag="worker_reconnect"
            setPadding(ui.dp(20),ui.dp(16),ui.dp(20),ui.dp(20))
            addView(ui.text(R.string.reconnect_worker,22f,true).apply {isAccessibilityHeading=true})
            addView(ui.text(R.string.reactivate_explanation,15f,color=ui.secondary).apply {setPadding(0,ui.dp(8),0,ui.dp(18))})
            addView(ui.button(R.string.reactivate_now,R.drawable.ic_worker,MobileUi.Tone.FILLED) {
                dialog?.dismiss();AppNavigation.openReactivation(activity,activateNow=true)
            }.apply {tag="reconnect_activate"})
            addView(ui.space(12))
            addView(ui.button(R.string.show_activation_guide,R.drawable.ic_setup,MobileUi.Tone.TONAL) {
                dialog?.dismiss();AppNavigation.openActivation(activity)
            }.apply {tag="reconnect_tutorial"})
            addView(ui.space(8))
            addView(ui.button(R.string.cancel,0,MobileUi.Tone.PLAIN) {PowerActivationRequest.cancel();dialog?.dismiss()})
        }
        return ui.sheet(body).also {dialog=it;it.setOnCancelListener {PowerActivationRequest.cancel()}}
    }
}
