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
        val enabled = !DeveloperOptionsControl.readyForActivation(this)
        if (enabled && DeveloperOptionsControl.needsWifi(this)) {
            // Wireless debugging cannot stay on without Wi-Fi; explain instead of a vague failure.
            Toast.makeText(this, UiPrefs.wrapLocale(this).getString(R.string.no_wifi_short), Toast.LENGTH_LONG).show(); refresh(); return
        }
        val success = DeveloperOptionsControl.setEnabled(this, enabled)
        if (!success) Toast.makeText(this, UiPrefs.wrapLocale(this).getString(R.string.dev_change_failed), Toast.LENGTH_LONG).show()
        // This tile tap is the explicit request. Discovery exists only for the
        // bounded activation session, never continuously or after boot.
        // Turning debugging on stays in the shade: Sukut tries to activate in the
        // background, and failing quietly is fine; the Activate button remains.
        if(success && enabled) DeveloperOptionsControl.activateQuietly(this)
        refresh()
    }
    private fun refresh() {
        val localized = UiPrefs.wrapLocale(this)
        qsTile?.apply {
            label = localized.getString(R.string.dev_tile_label)
            state = if (!DeveloperOptionsControl.available(this@DeveloperOptionsTileService)) Tile.STATE_UNAVAILABLE
                else if (DeveloperOptionsControl.readyForActivation(this@DeveloperOptionsTileService)) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
            subtitle = localized.getString(if (state == Tile.STATE_UNAVAILABLE) R.string.dev_tile_setup
                else if (DeveloperOptionsControl.readyForActivation(this@DeveloperOptionsTileService)) {if(DeveloperOptionsControl.usesWireless(this@DeveloperOptionsTileService)) R.string.dev_tile_wireless_on else R.string.dev_tile_usb_on}
                else if (state == Tile.STATE_ACTIVE) R.string.dev_tile_partial else R.string.dev_tile_off)
            contentDescription = "$label, $subtitle"
            updateTile()
        }
    }
    companion object {
        fun requestRefresh(context: android.content.Context) = requestListeningState(context, ComponentName(context, DeveloperOptionsTileService::class.java))
    }
}
