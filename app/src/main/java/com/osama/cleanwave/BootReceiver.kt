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
        // A reboot ends the shell worker. Only a fresh authorised ADB session can
        // activate it; never start an erroring foreground service or audio at boot.
        CleanerPrefs.setEnabled(context, false)
        CleanerPrefs.clearStartPending(context)
        CleanerPrefs.reconcileRuntimeState(context)
        CleanerTileService.requestRefresh(context)
    }
}
