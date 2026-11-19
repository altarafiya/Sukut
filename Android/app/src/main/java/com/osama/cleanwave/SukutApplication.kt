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
        watchWifi()
        // The worker came back after Sukut lost it while filtering: turn filtering on again.
        WorkerConnection.addListener { android.os.Handler(android.os.Looper.getMainLooper()).post { resumeIfWanted() } }
        AppIcon.repair(this)
    }
    // Phone activation runs over Wi-Fi: when Wi-Fi comes back, Sukut turns Wireless
    // debugging on again (if it may) and reconnects. Only while this process lives.
    private fun resumeIfWanted() {
        if (!WorkerConnection.ready || !CleanerPrefs.resumeAfterWorkerPending(this) || CleanerService.isAlive) return
        // Android may refuse a background start; then the next time Home opens it starts.
        PlaybackController.enable(this) { PowerActivationRequest.request() }
    }
    private fun watchWifi() {
        val manager = getSystemService(android.net.ConnectivityManager::class.java) ?: return
        val request = android.net.NetworkRequest.Builder().addTransportType(android.net.NetworkCapabilities.TRANSPORT_WIFI).build()
        runCatching {
            manager.registerNetworkCallback(request, object : android.net.ConnectivityManager.NetworkCallback() {
                override fun onAvailable(network: android.net.Network) {
                    android.os.Handler(android.os.Looper.getMainLooper()).post { DeveloperOptionsControl.wifiReturned(this@SukutApplication) }
                }
            })
        }
    }
    override fun onActivityStarted(activity: Activity) { uiVisible = ++started > 0 }
    override fun onActivityStopped(activity: Activity) {
        started = (started - 1).coerceAtLeast(0); uiVisible = started > 0
        // A new launcher icon is applied only after the user has left Sukut.
        if (started == 0 && !activity.isChangingConfigurations) AppIcon.applyPending(this)
    }
    override fun onActivityCreated(activity: Activity, state: Bundle?) = Unit
    override fun onActivityResumed(activity: Activity) = Unit
    override fun onActivityPaused(activity: Activity) = Unit
    override fun onActivitySaveInstanceState(activity: Activity, state: Bundle) = Unit
    override fun onActivityDestroyed(activity: Activity) = Unit
}
