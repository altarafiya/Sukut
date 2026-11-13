package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.view.View
import android.window.OnBackInvokedDispatcher

/** Paired push/pop directions use the app locale, including Arabic on English Android. */
internal object AppNavigation {
    private var activationHost = java.lang.ref.WeakReference<Activity>(null)
    fun registerActivationHost(activity: Activity) { activationHost = java.lang.ref.WeakReference(activity) }
    fun unregisterActivationHost(activity: Activity) { if (activationHost.get() === activity) activationHost.clear() }
    fun hasActivationHost() = activationHost.get()?.let { !it.isFinishing && !it.isDestroyed } == true
    /** Desktop launch resumes the current guide without changing its page. */
    fun resumeActivationHost(activity: Activity): Boolean {
        val host = activationHost.get()?.takeIf { !it.isFinishing && !it.isDestroyed && it !== activity } ?: return false
        activity.startActivity(Intent(activity, host.javaClass).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_SINGLE_TOP))
        return true
    }
    fun finishActivation(activity: Activity) {
        activity.startActivity(Intent(activity, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        activity.finish()
    }
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
    fun openActivation(activity: Activity, method: Int = UiPrefs.activationMethod(activity), action: String? = null, startStep: Int = -1) {
        activity.startActivity(Intent(activity, LocalActivationActivity::class.java).putExtra("method", method).putExtra("activation_action", action).putExtra("guide_start_step",startStep).putExtra("return_to_setup",activity is SetupActivity))
        if (Build.VERSION.SDK_INT < 34) {
            @Suppress("DEPRECATION")
            activity.overridePendingTransition(motion(if (rtl(activity)) R.anim.settings_enter_rtl else R.anim.settings_enter),
                motion(if (rtl(activity)) R.anim.home_exit_rtl else R.anim.home_exit))
        }
    }
    fun openReactivation(activity: Activity, activateNow: Boolean = false) {
        activity.startActivity(Intent(activity, ReactivationActivity::class.java).putExtra("activate_now",activateNow))
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
