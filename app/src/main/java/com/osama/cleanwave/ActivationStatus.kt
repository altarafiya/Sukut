package com.osama.cleanwave

import android.app.Activity
import android.view.Gravity
import android.widget.LinearLayout

/** Status explains the next action; colour never carries the meaning alone. */
internal object ActivationStatus {
    fun readyColor(context:android.content.Context,background:Int):Int {
        val dark=UiPrefs.isDark(context)
        return ThemeColors.readableAccent(if(dark)0xFF8BD5A7.toInt() else 0xFF247449.toInt(),background,dark)
    }
    const val DEVELOPER=0; const val USB=1; const val WORKER=2; const val WIRELESS=3
    fun card(activity:Activity,vararg indices:Int):LinearLayout {
        val ui=MobileUi(activity)
        val labels=intArrayOf(R.string.status_developer,R.string.status_usb,R.string.status_worker,R.string.status_wireless)
        val icons=intArrayOf(R.drawable.ic_developer_tile,R.drawable.ic_phone,R.drawable.ic_worker,R.drawable.ic_setup_wireless)
        val states=booleanArrayOf(DeveloperOptionsControl.developerEnabled(activity),DeveloperOptionsControl.usbEnabled(activity),WorkerConnection.ready,DeveloperOptionsControl.wirelessEnabled(activity))
        val ready=intArrayOf(R.string.dev_ready_detail,R.string.usb_ready_detail,R.string.worker_ready_detail,R.string.wireless_ready_detail)
        val needed=intArrayOf(R.string.dev_needed_detail,R.string.usb_needed_detail,R.string.worker_needed_detail,R.string.wireless_needed_detail)
        return ui.card(12).apply {
            tag="activation_status_pills"
            for(i in indices) {
                val color=if(states[i])readyColor(activity,ui.cardColor) else ui.warning
                addView(LinearLayout(activity).apply {
                    orientation=LinearLayout.VERTICAL;setPadding(0,ui.dp(6),0,ui.dp(6))
                    addView(LinearLayout(activity).apply {
                        gravity=Gravity.CENTER_VERTICAL
                        addView(ui.icon(icons[i],color,20))
                        addView(ui.text(labels[i],14f,true),LinearLayout.LayoutParams(0,-2,1f).apply {marginStart=ui.dp(8)})
                        addView(ui.text(if(i==WORKER) {if(states[i])R.string.status_ready else R.string.status_activation_needed} else if(i==DEVELOPER) {if(states[i])R.string.status_on_plural else R.string.status_off_plural} else if(states[i])R.string.status_on else R.string.status_off,12f,true,color).apply {
                            tag="activation_status_$i";background=ui.rounded(ThemeColors.mix(ui.cardColor,color,.10f),10)
                            setPadding(ui.dp(9),ui.dp(5),ui.dp(9),ui.dp(5))
                        })
                    })
                    addView(ui.text(if(states[i])ready[i] else needed[i],13f,color=color).apply {
                        tag="activation_status_detail_$i";setPaddingRelative(ui.dp(28),ui.dp(7),0,0)
                    })
                })
            }
        }
    }
}
