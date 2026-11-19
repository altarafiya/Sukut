package com.osama.cleanwave

import android.content.ComponentName
import android.database.ContentObserver
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.widget.Toast

/** Separate from the audio tile. Reads system truth only while the shade is open. */
class DeveloperOptionsTileService : TileService() {
    private var observing = false
    private val observer = object : ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange: Boolean) { refresh() }
    }
    override fun onTileAdded() { super.onTileAdded(); DeveloperOptionsControl.markTileAdded(this,true) }
    override fun onTileRemoved() { DeveloperOptionsControl.markTileAdded(this,false); super.onTileRemoved() }
    override fun onStartListening() {
        super.onStartListening()
        if (!observing) {
            listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, Settings.Global.ADB_ENABLED, "adb_wifi_enabled").forEach {
                contentResolver.registerContentObserver(Settings.Global.getUriFor(it), false, observer)
            }
            observing = true
        }
        refresh()
    }
    override fun onStopListening() { stopObserving(); super.onStopListening() }
    override fun onDestroy() { stopObserving(); super.onDestroy() }
    private fun stopObserving() { if (observing) { contentResolver.unregisterContentObserver(observer); observing = false } }
    override fun onClick() {
        super.onClick()
        // Enabling debugging always needs an unlocked phone and this user gesture.
        if (isLocked) unlockAndRun { toggle() } else toggle()
    }
    private fun toggle() {
        DeveloperOptionsControl.markTileAdded(this,true)
        if (!DeveloperOptionsControl.available(this)) {
            Toast.makeText(this, UiPrefs.wrapLocale(this).getString(R.string.dev_computer_required), Toast.LENGTH_LONG).show(); refresh(); return
        }
        // Developer mode: on means Developer options plus Wireless debugging; off turns
        // all of it off, so apps that refuse Developer options (banks) work again.
        val enabled = !DeveloperOptionsControl.developerEnabled(this)
        val result = DeveloperOptionsControl.setDeveloperMode(this, enabled)
        val localized = UiPrefs.wrapLocale(this)
        when (result) {
            DeveloperOptionsControl.DeveloperMode.FAILED -> Toast.makeText(this, localized.getString(R.string.dev_change_failed), Toast.LENGTH_LONG).show()
            // Developer options are on; Wireless debugging waits for Wi-Fi (no internet needed).
            DeveloperOptionsControl.DeveloperMode.NO_WIFI -> Toast.makeText(this, localized.getString(R.string.dev_mode_no_wifi), Toast.LENGTH_LONG).show()
            else -> Unit
        }
        // This tile tap is the explicit request. Discovery exists only for the
        // bounded activation session, never continuously or after boot.
        // Turning it on stays in the shade: Sukut tries to activate in the
        // background, and failing quietly is fine; the Activate button remains.
        if (enabled && result != DeveloperOptionsControl.DeveloperMode.FAILED && DeveloperOptionsControl.readyForActivation(this))
            DeveloperOptionsControl.activateQuietly(this)
        refresh()
    }
    private fun refresh() {
        val localized = UiPrefs.wrapLocale(this)
        qsTile?.apply {
            label = localized.getString(R.string.dev_tile_label)
            val service = this@DeveloperOptionsTileService
            val developer = DeveloperOptionsControl.developerEnabled(service)
            val ready = DeveloperOptionsControl.available(service)
            // Never STATE_UNAVAILABLE: Android ignores taps on it, so the tile looked dead
            // (Xiaomi Pad 6). Without the permission a tap explains how to prepare it.
            state = if (ready && developer) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
            subtitle = localized.getString(when {
                !ready -> R.string.dev_tile_setup
                !developer -> R.string.dev_tile_off
                DeveloperOptionsControl.wirelessEnabled(service) -> R.string.dev_tile_wireless_on
                DeveloperOptionsControl.usbEnabled(service) -> R.string.dev_tile_usb_on
                else -> R.string.dev_tile_partial
            })
            contentDescription = "$label, $subtitle"
            updateTile()
        }
    }
    companion object {
        fun requestRefresh(context: android.content.Context) = requestListeningState(context, ComponentName(context, DeveloperOptionsTileService::class.java))
    }
}
