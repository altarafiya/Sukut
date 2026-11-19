package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.drawable.Icon
import android.os.Build
import android.os.UserManager
import android.provider.Settings
import android.widget.Toast

/** Shell-provisioned, explicitly user-operated shortcut. No boot/background observer. */
internal object DeveloperOptionsControl {
    private const val WIFI = "adb_wifi_enabled"
    private fun prefs(context: Context) = context.getSharedPreferences("computer_activation", Context.MODE_PRIVATE)
    fun tileAdded(context: Context) = prefs(context).getBoolean("tile_added", false)
    fun markTileAdded(context: Context, added: Boolean) { prefs(context).edit().putBoolean("tile_added", added).apply() }
    fun available(context: Context) = context.checkSelfPermission(Manifest.permission.WRITE_SECURE_SETTINGS) == PackageManager.PERMISSION_GRANTED
    fun developerEnabled(context: Context) = Settings.Global.getInt(context.contentResolver, Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 0) != 0
    fun usbEnabled(context: Context) = Settings.Global.getInt(context.contentResolver, Settings.Global.ADB_ENABLED, 0) != 0
    fun wirelessEnabled(context: Context) = Build.VERSION.SDK_INT >= 30 && Settings.Global.getInt(context.contentResolver, WIFI, 0) != 0
    fun allOff(context: Context) = !developerEnabled(context) && !usbEnabled(context) && !wirelessEnabled(context)
    fun readyForUsb(context: Context) = developerEnabled(context) && usbEnabled(context)

    /** Only the shell-only provider calls this, after verifying the running worker. */
    fun computerReady(context: Context) {
        check(available(context))
        UiPrefs.setActivationMethod(context, WorkerGuide.WINDOWS)
        prefs(context).edit().remove("offer_cleanup").apply()
        DeveloperOptionsTileService.requestRefresh(context)
        WorkerConnection.refreshStatus()
    }
    fun phoneReady(context: Context) {
        check(available(context))
        UiPrefs.setActivationMethod(context, WorkerGuide.PHONE)
        dismissCleanup(context)
        DeveloperOptionsTileService.requestRefresh(context)
    }
    fun dismissCleanup(context: Context) { prefs(context).edit().remove("offer_cleanup").apply() }
    fun usesWireless(context: Context) = android.os.Build.VERSION.SDK_INT >= 30 && UiPrefs.activationMethod(context) == WorkerGuide.PHONE
    /**
     * Wireless debugging works only while this phone is joined to a Wi-Fi network. Any
     * network counts, with or without internet (another phone's hotspot is fine).
     */
    fun wifiConnected(context: Context): Boolean {
        val manager = context.getSystemService(android.net.ConnectivityManager::class.java) ?: return true
        @Suppress("DEPRECATION")
        return runCatching { manager.allNetworks.any { manager.getNetworkCapabilities(it)?.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) == true } }.getOrDefault(true)
    }
    /** Phone activation is chosen but there is no Wi-Fi: say so instead of failing silently. */
    fun needsWifi(context: Context) = usesWireless(context) && !wifiConnected(context)
    fun readyForActivation(context: Context) = developerEnabled(context) && if(usesWireless(context)) wirelessEnabled(context) else usbEnabled(context)

    enum class DeveloperMode { WIRELESS, USB, NO_WIFI, FAILED }
    /**
     * The Developer mode tile (0.5.9): one tap turns Developer options on together with
     * Wireless debugging (and USB debugging for the computer method), so activation is
     * one step away; the next tap turns everything off again, so banking and other apps
     * that refuse to run with Developer options on work normally.
     */
    fun setDeveloperMode(context: Context, on: Boolean): DeveloperMode {
        if (!on) return if (setEnabled(context, false)) DeveloperMode.USB else DeveloperMode.FAILED
        if (!available(context)) return DeveloperMode.FAILED
        if (context.getSystemService(UserManager::class.java).hasUserRestriction(UserManager.DISALLOW_DEBUGGING_FEATURES)) return DeveloperMode.FAILED
        val resolver = context.contentResolver
        fun write(key: String, value: Int) = runCatching { Settings.Global.putInt(resolver, key, value) }.getOrDefault(false)
        if (!write(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 1) || !developerEnabled(context)) {
            DeveloperOptionsTileService.requestRefresh(context); return DeveloperMode.FAILED
        }
        // The computer method reconnects over the cable: keep USB debugging ready too.
        if (!usesWireless(context)) write(Settings.Global.ADB_ENABLED, 1)
        val wifi = Build.VERSION.SDK_INT >= 30 && wifiConnected(context)
        if (wifi) write(WIFI, 1)
        DeveloperOptionsTileService.requestRefresh(context)
        return when {
            wifi && wirelessEnabled(context) -> DeveloperMode.WIRELESS
            Build.VERSION.SDK_INT >= 30 && !wifi -> DeveloperMode.NO_WIFI
            else -> DeveloperMode.USB
        }
    }

    /** No generic setting writer or remote command interface is exposed. */
    fun setEnabled(context: Context, enabled: Boolean): Boolean {
        if (!available(context)) return false
        if (enabled && context.getSystemService(UserManager::class.java).hasUserRestriction(UserManager.DISALLOW_DEBUGGING_FEATURES)) return false
        val resolver = context.contentResolver
        val previousDeveloper = developerEnabled(context)
        val previousUsb = usbEnabled(context)
        val previousWireless = wirelessEnabled(context)
        fun write(key: String, value: Int) = runCatching { Settings.Global.putInt(resolver, key, value) }.getOrDefault(false)
        val success = if (enabled) {
            val wireless = usesWireless(context)
            val transport = if(wireless) WIFI else Settings.Global.ADB_ENABLED
            val wroteDeveloper = write(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 1)
            val wroteTransport = wroteDeveloper && write(transport, 1)
            if (!wroteTransport || !readyForActivation(context)) {
                write(transport, if(if(wireless) previousWireless else previousUsb) 1 else 0)
                write(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, if (previousDeveloper) 1 else 0)
                false
            } else true
        } else {
            // Attempt every OFF write even if an OEM rejects one; verify readback.
            if (Build.VERSION.SDK_INT >= 30) write(WIFI, 0)
            write(Settings.Global.ADB_ENABLED, 0)
            write(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, 0)
            allOff(context)
        }
        DeveloperOptionsTileService.requestRefresh(context)
        return success
    }
    /** Wireless debugging off and on again, keeping the saved pairing. Needs the shell-granted permission. */
    fun restartWireless(context: Context, after: () -> Unit) {
        if (!available(context) || Build.VERSION.SDK_INT < 30) { after(); return }
        val resolver = context.contentResolver
        runCatching { Settings.Global.putInt(resolver, WIFI, 0) }
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
            runCatching { Settings.Global.putInt(resolver, WIFI, 1) }
            DeveloperOptionsTileService.requestRefresh(context); after()
        }, 1500)
    }

    /**
     * Tries to activate on the phone without opening Sukut (the Debugging tile, Wi-Fi
     * returning). Android may refuse a background start; that is fine, the user can
     * still tap Activate.
     */
    /** Wi-Fi is back: switch Wireless debugging on again if Sukut may, then try to activate. */
    fun wifiReturned(context: Context) {
        if (!UiPrefs.setupCompleted(context) || !usesWireless(context) || WorkerConnection.ready || !available(context)) return
        // Developer options off: switch everything back on only when Sukut was filtering when it lost Wi-Fi.
        if (!developerEnabled(context)) { if (!CleanerPrefs.resumeAfterWorkerPending(context) || !setEnabled(context, true)) return }
        else if (!wirelessEnabled(context)) runCatching { Settings.Global.putInt(context.contentResolver, WIFI, 1) }
        // Give Wireless debugging a moment to start listening on the new network.
        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({ activateQuietly(context) }, 2500)
    }
    fun activateQuietly(context: Context) {
        if (!usesWireless(context) || WorkerConnection.ready || LocalActivationState.busy || !readyForActivation(context)) return
        runCatching { context.startForegroundService(android.content.Intent(context, LocalActivationService::class.java).setAction(LocalActivationService.ACTIVATE).putExtra(LocalActivationService.QUIET, true)) }
    }
    fun changeFromGuide(activity: Activity, enabled: Boolean): Boolean {
        val success = setEnabled(activity, enabled)
        Toast.makeText(activity, if (success) {
            if (enabled) { if(usesWireless(activity)) R.string.dev_wireless_on else R.string.dev_usb_on } else R.string.dev_options_off
        } else R.string.dev_change_failed, Toast.LENGTH_LONG).show()
        return success
    }
    fun requestTile(activity: Activity, result: (Boolean) -> Unit) {
        if (Build.VERSION.SDK_INT < 33) { result(false); return }
        runCatching {
            activity.getSystemService(StatusBarManager::class.java).requestAddTileService(
                ComponentName(activity, DeveloperOptionsTileService::class.java), activity.getString(R.string.dev_tile_label),
                Icon.createWithResource(activity, R.drawable.ic_developer_tile), activity.mainExecutor
            ) { code ->
                val added = code == StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ADDED || code == StatusBarManager.TILE_ADD_REQUEST_RESULT_TILE_ALREADY_ADDED
                if(added) markTileAdded(activity, true)
                result(added)
            }
        }.onFailure { result(false) }
    }
}
