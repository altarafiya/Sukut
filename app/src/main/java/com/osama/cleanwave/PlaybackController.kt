package com.osama.cleanwave

import android.app.ForegroundServiceStartNotAllowedException
import android.content.Context
import android.content.Intent
import android.os.Build

/** Shared Home/tile commands, including a cancellable pending service start. */
internal object PlaybackController {
    /**
     * [onBlocked]: Android 12+ refuses a foreground service started while Sukut
     * is in the background (the Quick Settings tile with the app closed). The
     * caller then retries from a foreground activity instead of failing.
     */
    fun enable(context: Context, onBlocked: (() -> Unit)? = null) {
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
            if (onBlocked != null && Build.VERSION.SDK_INT >= 31 && problem is ForegroundServiceStartNotAllowedException) {
                CleanerPrefs.setStatus(context, "Off")
                CleanerTileService.requestRefresh(context)
                onBlocked()
                return
            }
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
            WorkerBackend.stopExisting(context) {
                CleanerPrefs.clearStartPending(context); CleanerPrefs.setStatus(context, "Off")
                CleanerTileService.requestRefresh(context)
            }
        }
    }
}
