package com.osama.cleanwave

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent?) {
        if (intent?.action != Intent.ACTION_BOOT_COMPLETED) return
        // Android 15+ also delivers BOOT_COMPLETED after a force-stopped app
        // is reopened. That must not re-enable a stale saved audio route.
        val newBoot = CleanerPrefs.consumeNewBoot(context)
        Log.i("CleanWaveLifecycle", "Boot broadcast: newBoot=$newBoot")
        if (!newBoot) {
            CleanerPrefs.reconcileRuntimeState(context)
            CleanerTileService.requestRefresh(context)
            return
        }
        if (!CleanerPrefs.restoreOnBoot(context) || !CleanerPrefs.isEnabled(context)) return

        val service = Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_ENABLE)
        if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(service) else context.startService(service)
    }
}
