package com.osama.cleanwave

import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.service.quicksettings.TileService

/**
 * Long-press on one of Sukut's Quick Settings tiles. Holding the Developer mode tile
 * turns Developer options on (if they are off and Sukut may) and opens the Developer
 * options page; holding the other tiles opens Sukut, as before. No UI of its own.
 */
class TilePreferencesActivity : Activity() {
    override fun onCreate(state: Bundle?) {
        super.onCreate(state)
        val tile: ComponentName? = if (Build.VERSION.SDK_INT >= 33)
            intent.getParcelableExtra(Intent.EXTRA_COMPONENT_NAME, ComponentName::class.java)
        else @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_COMPONENT_NAME)
        if (tile?.className == DeveloperOptionsTileService::class.java.name) openDeveloperOptions()
        else startActivity(Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP))
        finish()
    }

    private fun openDeveloperOptions() {
        // The Developer options page only opens while Developer options are on.
        if (!DeveloperOptionsControl.developerEnabled(this) && DeveloperOptionsControl.available(this))
            DeveloperOptionsControl.setDeveloperMode(this, true)
        val developer = Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (DeveloperOptionsControl.developerEnabled(this) && runCatching { startActivity(developer) }.isSuccess) return
        // Developer options are still off (no permission yet): About phone, where they are unlocked.
        runCatching { startActivity(Intent(Settings.ACTION_DEVICE_INFO_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
            .onFailure { startActivity(Intent(Settings.ACTION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
        TileService.requestListeningState(this, ComponentName(this, DeveloperOptionsTileService::class.java))
    }
}
