package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.view.View
import android.window.OnBackInvokedDispatcher

/** Paired push/pop directions use the app locale, including Arabic on English Android. */
internal object AppNavigation {
    private fun rtl(activity: Activity) = activity.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
    private fun motion(animation: Int) = if (android.animation.ValueAnimator.areAnimatorsEnabled()) animation else 0
    fun openSettings(activity: Activity, audio: Boolean = false) {
        activity.startActivity(Intent(activity, SettingsActivity::class.java).putExtra("audio", audio))
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.settings_enter_rtl else R.anim.settings_enter),
                motion(if (rtl(activity)) R.anim.home_exit_rtl else R.anim.home_exit))
        }
    }
    fun configureSettings(activity: Activity) = configurePage(activity)
    fun configurePage(activity: Activity) {
        if (Build.VERSION.SDK_INT >= 34) {
            val arabic = rtl(activity)
            activity.overrideActivityTransition(Activity.OVERRIDE_TRANSITION_OPEN,
                motion(if (arabic) R.anim.settings_enter_rtl else R.anim.settings_enter),
                motion(if (arabic) R.anim.home_exit_rtl else R.anim.home_exit))
            activity.overrideActivityTransition(Activity.OVERRIDE_TRANSITION_CLOSE,
                motion(if (arabic) R.anim.home_return_rtl else R.anim.home_return),
                motion(if (arabic) R.anim.settings_exit_rtl else R.anim.settings_exit))
        }
    }
    fun openSetup(activity: Activity) {
        activity.startActivity(Intent(activity, SetupActivity::class.java))
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.settings_enter_rtl else R.anim.settings_enter),
                motion(if (rtl(activity)) R.anim.home_exit_rtl else R.anim.home_exit))
        }
    }
    fun openActivation(activity: Activity, method: Int = UiPrefs.activationMethod(activity), action: String? = null) {
        activity.startActivity(Intent(activity, LocalActivationActivity::class.java).putExtra("method", method).putExtra("activation_action", action).putExtra("return_to_setup",activity is SetupActivity))
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.settings_enter_rtl else R.anim.settings_enter),
                motion(if (rtl(activity)) R.anim.home_exit_rtl else R.anim.home_exit))
        }
    }
    fun closeSetup(activity: Activity) {
        activity.finish()
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.home_return_rtl else R.anim.home_return),
                motion(if (rtl(activity)) R.anim.settings_exit_rtl else R.anim.settings_exit))
        }
    }
    fun closeSettings(activity: Activity) {
        if (activity.isTaskRoot) activity.startActivity(Intent(activity, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
        activity.finish()
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.home_return_rtl else R.anim.home_return),
                motion(if (rtl(activity)) R.anim.settings_exit_rtl else R.anim.settings_exit))
        }
    }

    /**
     * Framework back handling without androidx.activity. Android 13+ uses the
     * predictive-back dispatcher; earlier versions call the activity's
     * onBackPressed override, which forwards to the same [action].
     */
    fun handleBack(activity: Activity, action: () -> Unit) {
        if (Build.VERSION.SDK_INT >= 33) activity.onBackInvokedDispatcher
            .registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT) { action() }
    }
}
