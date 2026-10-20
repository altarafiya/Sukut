package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.view.View

/** Paired push/pop directions use the app locale, including Arabic on English Android. */
internal object AppNavigation {
    private fun rtl(activity: Activity) = activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
    fun openSettings(activity: Activity, audio: Boolean = false) {
        activity.startActivity(Intent(activity, SettingsActivity::class.java).putExtra("audio", audio))
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(if (rtl(activity)) R.anim.settings_enter_rtl else R.anim.settings_enter,
                if (rtl(activity)) R.anim.home_exit_rtl else R.anim.home_exit)
        }
    }
    fun configureSettings(activity: Activity) {
        if (Build.VERSION.SDK_INT >= 34) {
            val arabic = rtl(activity)
            activity.overrideActivityTransition(Activity.OVERRIDE_TRANSITION_OPEN,
                if (arabic) R.anim.settings_enter_rtl else R.anim.settings_enter,
                if (arabic) R.anim.home_exit_rtl else R.anim.home_exit)
            activity.overrideActivityTransition(Activity.OVERRIDE_TRANSITION_CLOSE,
                if (arabic) R.anim.home_return_rtl else R.anim.home_return,
                if (arabic) R.anim.settings_exit_rtl else R.anim.settings_exit)
        }
    }
    fun closeSettings(activity: Activity) {
        if (activity.isTaskRoot) activity.startActivity(Intent(activity, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
        activity.finish()
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(if (rtl(activity)) R.anim.home_return_rtl else R.anim.home_return,
                if (rtl(activity)) R.anim.settings_exit_rtl else R.anim.settings_exit)
        }
    }
}
