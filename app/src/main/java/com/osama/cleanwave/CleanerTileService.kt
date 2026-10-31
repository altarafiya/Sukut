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

class CleanerTileService : TileService() {
    companion object {
        fun requestRefresh(context: Context) = requestListeningState(context, ComponentName(context, CleanerTileService::class.java))
    }
    private var listening = false
    private val handler = Handler(Looper.getMainLooper())
    private val update = Runnable { if (listening) refresh() }
    private val preferenceListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (ControlPlanePolicy.affectsTile(key)) queueRefresh()
    }
    private var lastTileState = ""
    private val received = WorkerConnection.OnChanged { queueRefresh() }
    private val dead = WorkerConnection.OnChanged { queueRefresh() }
    private val tileIcon by lazy { Icon.createWithResource(this, R.drawable.ic_sukut_tile) }

    override fun onStartListening() {
        super.onStartListening()
        lastTileState = "" // SystemUI may have recreated its tile while we weren't listening.
        if (!listening) {
            listening = true
            CleanerPrefs.registerListener(this, preferenceListener)
            WorkerConnection.addListener(received); WorkerConnection.addListener(dead)
        }
        refresh()
    }
    override fun onStopListening() { stopListening(); super.onStopListening() }
    override fun onDestroy() { stopListening(); super.onDestroy() }
    private fun stopListening() {
        if (!listening) return
        listening = false; handler.removeCallbacksAndMessages(null)
        CleanerPrefs.unregisterListener(this, preferenceListener)
        WorkerConnection.removeListener(received); WorkerConnection.removeListener(dead)
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
    private fun ready() = when (WorkerBackend.availabilityState()) {
        WorkerBackend.Companion.Availability.READY -> true
        else -> false
    }
    // The PendingIntent overload exists only from API 34. The guarded legacy
    // branch is required on API 29–33; lint's target-SDK check ignores that guard.
    @android.annotation.SuppressLint("StartActivityAndCollapseDeprecated")
    @Suppress("DEPRECATION") private fun openGuide() {
        val completed = UiPrefs.setupCompleted(this)
        val intent = Intent(this, if (completed) MainActivity::class.java else SetupActivity::class.java)
            .putExtra("reconnect", completed)
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
            !ready() -> ui.getString(R.string.reconnect_worker)
            else -> ui.getString(R.string.off)
        }
        val nextState = when { status == "Stopping" -> Tile.STATE_UNAVAILABLE; enabled -> Tile.STATE_ACTIVE; else -> Tile.STATE_INACTIVE }
        val signature = "$nextState:$description:${ui.getString(R.string.app_name)}"
        if (signature == lastTileState) return
        qsTile?.apply {
            state = nextState
            label = ui.getString(R.string.app_name); subtitle = description; icon = tileIcon
            contentDescription = "$label · $description"
            updateTile()
            lastTileState = signature
        }
    }
}
