package com.osama.cleanwave

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService

/**
 * A Quick Settings tile that switches between Music Filter and Strict. It is only
 * offered in Android's tile editor; the app itself never mentions or requests it.
 * Switching while Sukut runs applies at once; while Off it only changes the mode.
 */
class ModeSwitchTileService : TileService() {
    override fun onStartListening() { super.onStartListening(); refresh() }
    override fun onClick() {
        super.onClick()
        val next = if (CleanerPrefs.sourcePreset(this) == CleanerPrefs.SOURCE_STRICT) CleanerPrefs.SOURCE_MUSIC_FILTER else CleanerPrefs.SOURCE_STRICT
        CleanerPrefs.setSourcePreset(this, next)
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(this))
            runCatching { startService(Intent(this, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE)) }
        refresh()
        requestListeningState(this, ComponentName(this, CleanerTileService::class.java))
    }
    private fun refresh() {
        val strict = CleanerPrefs.sourcePreset(this) == CleanerPrefs.SOURCE_STRICT
        val text = UiPrefs.wrapLocale(this)
        qsTile?.apply {
            label = text.getString(if (strict) R.string.strict else R.string.music_filter)
            subtitle = text.getString(R.string.app_name)
            icon = Icon.createWithResource(this@ModeSwitchTileService, if (strict) R.drawable.ic_lock else R.drawable.ic_music_filter)
            state = if (CleanerPrefs.isEnabled(this@ModeSwitchTileService)) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
            contentDescription = "$label, $subtitle"
            updateTile()
        }
    }
    companion object {
        fun requestRefresh(context: Context) = runCatching { requestListeningState(context, ComponentName(context, ModeSwitchTileService::class.java)) }
    }
}
