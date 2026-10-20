package com.osama.cleanwave

import android.app.PendingIntent
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import rikka.shizuku.Shizuku

class CleanerTileService : TileService() {
    companion object {
        fun requestRefresh(context: Context) = requestListeningState(context, ComponentName(context, CleanerTileService::class.java))
    }
    private var listening = false
    private val handler = Handler(Looper.getMainLooper())
    private val update = Runnable { if (listening) refresh() }
    private val preferenceListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> queueRefresh() }
    private val received = Shizuku.OnBinderReceivedListener { queueRefresh() }
    private val dead = Shizuku.OnBinderDeadListener { queueRefresh() }
    private val tileIcon by lazy { Icon.createWithResource(this, R.drawable.ic_sukut_tile) }

    override fun onStartListening() {
        super.onStartListening()
        if (!listening) {
            listening = true
            CleanerPrefs.registerListener(this, preferenceListener)
            Shizuku.addBinderReceivedListenerSticky(received); Shizuku.addBinderDeadListener(dead)
        }
        refresh()
    }
    override fun onStopListening() { stopListening(); super.onStopListening() }
    override fun onDestroy() { stopListening(); super.onDestroy() }
    private fun stopListening() {
        if (!listening) return
        listening = false; handler.removeCallbacksAndMessages(null)
        CleanerPrefs.unregisterListener(this, preferenceListener)
        Shizuku.removeBinderReceivedListener(received); Shizuku.removeBinderDeadListener(dead)
    }
    private fun queueRefresh() { handler.removeCallbacks(update); handler.post(update) }
    override fun onClick() {
        super.onClick()
        CleanerPrefs.reconcileRuntimeState(this)
        if (CleanerPrefs.status(this) == "Stopping") return
        if (CleanerPrefs.isEnabled(this) || CleanerPrefs.startPending(this)) {
            PlaybackController.disable(this)
        } else if (!UiPrefs.setupCompleted(this) || !ready()) {
            openGuide()
        } else if (isLocked) {
            unlockAndRun { if (ready()) PlaybackController.enable(this) else openGuide() }
        } else PlaybackController.enable(this)
        refresh()
    }
    private fun ready() = when (ShizukuBackend.availabilityState()) {
        ShizukuBackend.Companion.Availability.READY_SHELL, ShizukuBackend.Companion.Availability.READY_ROOT,
        ShizukuBackend.Companion.Availability.READY_OTHER -> true
        else -> false
    }
    // The PendingIntent overload exists only from API 34. The guarded legacy
    // branch is required on API 29–33; lint's target-SDK check ignores that guard.
    @android.annotation.SuppressLint("StartActivityAndCollapseDeprecated")
    @Suppress("DEPRECATION") private fun openGuide() {
        val intent = Intent(this, SetupActivity::class.java).putExtra("page", if (UiPrefs.setupCompleted(this)) 1 else 0)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TOP)
        if (Build.VERSION.SDK_INT >= 34) startActivityAndCollapse(PendingIntent.getActivity(this, 37, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE))
        else startActivityAndCollapse(intent)
    }
    private fun refresh() {
        CleanerPrefs.reconcileRuntimeState(this)
        val enabled = CleanerPrefs.isEnabled(this)
        val status = CleanerPrefs.status(this)
        val ui = UiPrefs.wrapLocale(this)
        val description = when {
            status == "Stopping" -> ui.getString(R.string.stopping)
            enabled && status == "Starting privileged audio route" -> ui.getString(R.string.starting)
            enabled -> ui.getString(AudioModes.mode(CleanerPrefs.sourcePreset(this)).label)
            !ready() -> ui.getString(R.string.run_setup)
            else -> ui.getString(R.string.off)
        }
        qsTile?.apply {
            state = when { status == "Stopping" -> Tile.STATE_UNAVAILABLE; enabled -> Tile.STATE_ACTIVE; else -> Tile.STATE_INACTIVE }
            label = ui.getString(R.string.app_name); subtitle = description; icon = tileIcon
            contentDescription = "$label · $description"
            updateTile()
        }
    }
}
