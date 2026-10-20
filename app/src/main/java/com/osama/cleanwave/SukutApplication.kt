package com.osama.cleanwave

import android.app.Activity
import android.app.Application
import android.os.Bundle

/** Only active audio polls, less often when no app screen is visible. */
class SukutApplication : Application(), Application.ActivityLifecycleCallbacks {
    companion object {
        @Volatile var uiVisible = false
            private set
    }
    private var started = 0
    override fun onCreate() {
        super.onCreate()
        UiPrefs.prepareAppearance(this)
        CleanerPrefs.prepareNewEngine(this)
        registerActivityLifecycleCallbacks(this)
    }
    override fun onActivityStarted(activity: Activity) { uiVisible = ++started > 0 }
    override fun onActivityStopped(activity: Activity) { started = (started - 1).coerceAtLeast(0); uiVisible = started > 0 }
    override fun onActivityCreated(activity: Activity, state: Bundle?) = Unit
    override fun onActivityResumed(activity: Activity) = Unit
    override fun onActivityPaused(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
    override fun onActivityDestroyed(activity: Activity) = Unit
}
