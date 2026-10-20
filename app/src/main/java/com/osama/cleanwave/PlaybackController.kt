package com.osama.cleanwave

import android.content.Context
import android.content.Intent

/** Shared Home/tile commands, including a cancellable pending service start. */
internal object PlaybackController {
    fun enable(context: Context) {
        CleanerPrefs.prepareNewEngine(context)
        if (CleanerPrefs.status(context) == "Stopping") return
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(context)) {
            context.startService(Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
            return
        }
        if (CleanerPrefs.startPending(context)) return
        CleanerPrefs.beginStart(context)
        CleanerTileService.requestRefresh(context)
        try { context.startForegroundService(Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_ENABLE)) }
        catch (problem: Exception) {
            CleanerPrefs.clearStartPending(context); CleanerPrefs.setEnabled(context, false)
            CleanerPrefs.setStatus(context, "Could not start", problem.message ?: context.getString(R.string.audio_issue))
            CleanerTileService.requestRefresh(context)
        }
    }
    fun disable(context: Context) {
        val hasWorker = CleanerService.isAlive || CleanerPrefs.startPending(context)
        CleanerPrefs.beginStop(context)
        CleanerTileService.requestRefresh(context)
        if (hasWorker) {
            val intent = Intent(context, CleanerService::class.java).setAction(CleanerService.ACTION_DISABLE)
            try {
                if (CleanerService.isAlive) context.startService(intent) else context.startForegroundService(intent)
            } catch (problem: Exception) {
                CleanerPrefs.clearStartPending(context)
                CleanerPrefs.setStatus(context, "Could not stop", problem.message ?: context.getString(R.string.audio_issue))
            }
        } else {
            ShizukuBackend.stopExisting(context) {
                CleanerPrefs.clearStartPending(context); CleanerPrefs.setStatus(context, "Off")
                CleanerTileService.requestRefresh(context)
            }
        }
    }
}
