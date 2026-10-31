package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.app.NotificationManager
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View

/** Shared activation controller; onboarding never launches a duplicate guide. */
internal class LocalActivationFlow(private val activity: Activity, state: Bundle?, private val changed: () -> Unit, private val ready: () -> Unit) {
    private var pendingAction = state?.getString("pending_action")
    private var pendingSettings = state?.getBoolean("pending_settings") ?: false
    private var pairSettingsPending = state?.getBoolean("pair_settings_pending") ?: false
    private var returning = state?.getBoolean("return_after_connection") ?: false
    private var resumed = false
    private val listener: () -> Unit = { changed(); openSettingsIfReady(); returnIfReady() }
    private val worker = WorkerConnection.OnChanged { activity.runOnUiThread { listener() } }
    init { LocalActivationState.add(listener); WorkerConnection.addListener(worker) }
    fun notificationsAllowed(): Boolean {
        if (Build.VERSION.SDK_INT >= 33 && activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) return false
        val manager = activity.getSystemService(NotificationManager::class.java)
        return manager.areNotificationsEnabled() && manager.getNotificationChannel(LocalActivationService.CHANNEL)?.importance != NotificationManager.IMPORTANCE_NONE
    }
    val notificationsBlocked get() = pendingAction != null && !notificationsAllowed()
    val awaitingWorker get() = returning
    fun notificationButton(ui: MobileUi): View = ui.button(R.string.local_allow_notifications, R.drawable.ic_notifications, MobileUi.Tone.TONAL) {
        val manager = activity.getSystemService(NotificationManager::class.java)
        val intent = if (manager.getNotificationChannel(LocalActivationService.CHANNEL) != null)
            Intent(Settings.ACTION_CHANNEL_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_CHANNEL_ID, LocalActivationService.CHANNEL)
        else Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
        activity.startActivity(intent.putExtra(Settings.EXTRA_APP_PACKAGE, activity.packageName))
    }.apply { visibility = View.GONE }
    fun begin(action: String, openSettings: Boolean = false) {
        if (Build.VERSION.SDK_INT < 30 || action !in listOf(LocalActivationService.PAIR, LocalActivationService.ACTIVATE)) return
        pendingSettings = openSettings
        if (Build.VERSION.SDK_INT >= 33 && activity.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            pendingAction = action; activity.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 42); changed(); return
        }
        if (!notificationsAllowed()) { pendingAction = action; changed(); return }
        pendingAction = null; pairSettingsPending = openSettings; returning = true
        LocalActivationState.update(if (openSettings) R.string.local_pair_wait else R.string.local_searching, true)
        try { activity.startForegroundService(Intent(activity, LocalActivationService::class.java).setAction(action)) }
        catch (problem: RuntimeException) { pairSettingsPending = false; LocalActivationState.update(R.string.local_activate_failed, false) }
    }
    fun permissionsResult(code: Int, results: IntArray) {
        if (code != 42) return
        val action = pendingAction
        if (results.firstOrNull() == PackageManager.PERMISSION_GRANTED && action != null) begin(action, pendingSettings) else changed()
    }
    private fun openSettingsIfReady() {
        if (!resumed || !pairSettingsPending || !LocalActivationState.busy || !LocalActivationState.notificationReady || activity.isFinishing) return
        pairSettingsPending = false; AudioTools.openWirelessDebugging(activity, 704)
    }
    private fun returnIfReady() {
        if (!returning || pendingAction != null || pairSettingsPending || !WorkerConnection.ready || LocalActivationState.busy) return
        @Suppress("DEPRECATION")
        activity.finishActivity(704)
        if (resumed) { returning = false; ready() }
    }
    fun resume() {
        resumed = true
        pendingAction?.takeIf { notificationsAllowed() }?.let { begin(it, pendingSettings) }
        listener()
    }
    fun pause() { resumed = false }
    fun cancel(keepReturn: Boolean = false) {
        pendingAction = null; pendingSettings = false; pairSettingsPending = false; returning = returning && keepReturn
        if (LocalActivationState.busy) activity.startService(Intent(activity, LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
        changed()
    }
    fun save(state: Bundle) {
        state.putString("pending_action", pendingAction); state.putBoolean("pending_settings", pendingSettings)
        state.putBoolean("pair_settings_pending", pairSettingsPending); state.putBoolean("return_after_connection", returning)
    }
    fun destroy() { LocalActivationState.remove(listener); WorkerConnection.removeListener(worker) }
}
