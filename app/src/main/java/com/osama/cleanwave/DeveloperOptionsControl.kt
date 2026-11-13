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
    fun readyForActivation(context: Context) = developerEnabled(context) && if(usesWireless(context)) wirelessEnabled(context) else usbEnabled(context)

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
