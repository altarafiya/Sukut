package com.osama.cleanwave

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.SystemClock
import android.util.Log

class CleanerService : Service() {
    companion object {
        const val ACTION_ENABLE = "com.osama.cleanwave.action.ENABLE"
        const val ACTION_DISABLE = "com.osama.cleanwave.action.DISABLE"
        const val ACTION_CONFIGURE = "com.osama.cleanwave.action.CONFIGURE"
        const val CHANNEL_ID = "cleanwave_processing"
        const val NOTIFICATION_ID = 1001
        /** Quick taps through the modes restart once, after the last tap. */
        private const val RESTART_SETTLE_MS = 350L
        /** A new output device settles (and Android stops moving the track) before the rebuild. */
        private const val ROUTE_SETTLE_MS = 800L

        @Volatile
        var isAlive: Boolean = false
            private set
        @Volatile internal var routeGeneration: Long = 0
            private set
    }

    private var backend: WorkerBackend? = null
    private var failed = false
    private var activeMode = MusicRemovalProcessor.DIALOGUE
    private var activeBuffering = 0
    private var activeEffects = false
    private var activeSource = ""
    private var lastFallbacks = 0L
    private var lastRecoveries = 0L
    private var lastProbeFailures = 0L
    private var lastUnderruns = 0L
    private val playbackAdvice = PlaybackAdvicePolicy()
    private val packetPattern = Regex("\\bpackets=(\\d+)")
    private val counterPattern = Regex("(?:^|; )(?<name>fallback_events|recoveries|recovery_probe_failures|output_underruns)=(?<value>[0-9]+)")
    private var destroyed = false
    private val handler = Handler(Looper.getMainLooper())
    /** The output device class (speaker, Bluetooth, wired) the running route plays to. */
    private var outputClass = -1
    /** Output underruns seen while playback progresses; a clean restart heals a starved route. */
    private val underrunPolls = java.util.ArrayDeque<Long>()
    private var lastAutoRestart = 0L
    private var routeStartedAt = 0L
    private var lastPackets = -1L
    /** One clean rebuild, applied after taps and route changes have settled. */
    private val restart = Runnable { restartNow() }
    private var switchingMode = false
    private var routeStarted = false
    private var notifiedBypass = false
    private var stopRequested = false

    @SuppressLint("ForegroundServiceType") // Merged manifest declares specialUse; AGP 8.7 lint predates compileSdk 36.
    override fun onCreate() {
        super.onCreate()
        CleanerPrefs.prepareNewEngine(this)
        CleanerPrefs.rememberCurrentBoot(this)
        isAlive = true
        if (!CleanerPrefs.cancelledStart(this)) CleanerPrefs.setEnabled(this, true)
        CleanerPrefs.clearStartPending(this)
        createChannel()
        startForeground(NOTIFICATION_ID, notification(uiString(R.string.notification_starting)))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_DISABLE || CleanerPrefs.cancelledStart(this)) {
            requestStop()
            return START_NOT_STICKY
        }
        if (stopRequested) return START_NOT_STICKY
        if (intent?.action == ACTION_CONFIGURE) {
            if (backend != null && (activeMode != CleanerPrefs.processingMode(this) ||
                    activeBuffering != CleanerPrefs.buffering(this) || activeEffects != CleanerPrefs.effectsCompatibility(this) ||
                    activeSource != CleanerPrefs.sourceApp(this))) {
                scheduleRestart(RESTART_SETTLE_MS)
            } else {
                backend?.updateMusicReduction()
                updateNotification(uiString(if (notifiedBypass) R.string.recovering else R.string.notification_active))
                CleanerTileService.requestRefresh(this)
            }
            return START_STICKY
        }
        if (backend != null) return START_STICKY

        startBackend()
        return START_STICKY
    }

    private fun requestStop() {
        if (stopRequested) return
        stopRequested = true
        CleanerPrefs.beginStop(this)
        CleanerTileService.requestRefresh(this)
        val current = backend
        backend = null
        if (current == null) {
            WorkerBackend.stopExisting(this) { finishStop() }
        } else {
            current.stop(afterStopped = { finishStop() })
        }
    }

    private fun finishStop() {
        if (destroyed) return
        CleanerPrefs.setStatus(this, "Off")
        CleanerTileService.requestRefresh(this)
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun startBackend() {
        if (destroyed || stopRequested || switchingMode || backend != null) return
        activeMode = CleanerPrefs.processingMode(this)
        activeBuffering = CleanerPrefs.buffering(this)
        activeEffects = CleanerPrefs.effectsCompatibility(this)
        activeSource = CleanerPrefs.sourceApp(this)
        lastFallbacks = 0; lastRecoveries = 0; lastProbeFailures = 0; lastUnderruns = 0
        playbackAdvice.reset()
        CleanerPrefs.resetRecovery(this)
        CleanerPrefs.setStatus(this, "Starting privileged audio route")
        val next = WorkerBackend(this, object : WorkerBackend.Listener {
            override fun onOutputRoute(deviceId: Int, deviceType: Int) {
                if (destroyed || stopRequested) return
                val optionsChanged = CleanerPrefs.observeOutputRoute(this@CleanerService, deviceId, deviceType)
                val kind = CleanerPrefs.outputClass(deviceType)
                val moved = outputClass >= 0 && kind != outputClass
                outputClass = kind
                // Bluetooth, speaker, wired: Android moves the playing track, but its queued
                // reserve and size can be lost on the way and the sound can starve. Rebuild
                // the route once, cleanly, after the new device has settled.
                if (optionsChanged || moved) scheduleRestart(ROUTE_SETTLE_MS)
            }

            override fun onStarted(detail: String) {
                if (destroyed || stopRequested || CleanerPrefs.cancelledStart(this@CleanerService)) return
                switchingMode = false
                routeGeneration++
                routeStarted = true
                routeStartedAt = SystemClock.elapsedRealtime(); underrunPolls.clear(); lastPackets = -1
                notifiedBypass = false
                CleanerPrefs.setEnabled(this@CleanerService, true)
                CleanerPrefs.setStatus(this@CleanerService, "Active", detail)
                updateNotification(uiString(R.string.notification_active))
                CleanerTileService.requestRefresh(this@CleanerService)
            }

            override fun onFailed(reason: String) {
                if (destroyed || stopRequested) return
                failed = true
                val failureStatus = if (routeStarted) {
                    "Audio route stopped"
                } else {
                    "Could not start"
                }
                CleanerPrefs.setEnabled(this@CleanerService, false)
                // Sukut was filtering and lost its worker: come back on by itself once it returns.
                if (routeStarted) CleanerPrefs.markResumeAfterWorker(this@CleanerService)
                CleanerPrefs.setStatus(this@CleanerService, failureStatus, reason)
                updateNotification(ErrorText.friendly(UiPrefs.wrapLocale(this@CleanerService), reason))
                CleanerTileService.requestRefresh(this@CleanerService)
                stopForeground(STOP_FOREGROUND_REMOVE)
                stopSelf()
            }

            override fun onStopped() = Unit

            override fun onProgress(detail: String, metrics: String) {
                if (destroyed || stopRequested || CleanerPrefs.cancelledStart(this@CleanerService)) return
                CleanerPrefs.setStatus(this@CleanerService, "Active", detail)
                if (metrics.isNotBlank()) recordRecovery(metrics)
                val bypassed = detail.startsWith("AI temporarily bypassed")
                if (bypassed && !notifiedBypass) {
                    notifiedBypass = true
                    updateNotification(uiString(R.string.notification_busy))
                } else if (!bypassed && notifiedBypass) {
                    notifiedBypass = false
                    updateNotification(uiString(R.string.notification_active))
                }
                // Standby is an audio state, not a notification event. Keep the
                // user's ongoing notification stable as playback sleeps/wakes.
            }
        })
        backend = next
        next.start()
    }

    private fun scheduleRestart(delay: Long) {
        if (destroyed || stopRequested || backend == null) return
        handler.removeCallbacks(restart); handler.postDelayed(restart, delay)
    }
    /** Rebuild capture and playback with the current settings and a fresh reserve. */
    private fun restartNow() {
        if (destroyed || stopRequested || backend == null) return
        activeMode = CleanerPrefs.processingMode(this)
        activeBuffering = CleanerPrefs.buffering(this)
        activeEffects = CleanerPrefs.effectsCompatibility(this)
        activeSource = CleanerPrefs.sourceApp(this)
        lastFallbacks = 0; lastRecoveries = 0; lastProbeFailures = 0; lastUnderruns = 0
        underrunPolls.clear(); lastPackets = -1
        playbackAdvice.reset()
        CleanerPrefs.resetRecovery(this)
        switchingMode = true
        CleanerPrefs.setStatus(this, "Starting privileged audio route")
        updateNotification(uiString(R.string.notification_starting))
        backend?.restartRoute()
    }

    override fun onDestroy() {
        destroyed = true
        handler.removeCallbacks(restart)
        backend?.stop()
        backend = null
        isAlive = false
        CleanerPrefs.setEnabled(this, false)
        if (!failed) CleanerPrefs.setStatus(this, "Off")
        CleanerTileService.requestRefresh(this)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createChannel() {
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, uiString(R.string.notification_channel), NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun notification(text: String): Notification {
        val open = PendingIntent.getActivity(
            this,
            1,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val turnOff = PendingIntent.getService(
            this,
            2,
            Intent(this, CleanerService::class.java).setAction(ACTION_DISABLE),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_sukut_tile)
            .setContentTitle(uiString(R.string.app_name))
            .setContentText(modeNotification(text))
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, uiString(R.string.turn_off), turnOff).build())
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setOngoing(true)
            .build()
    }

    private fun uiString(id: Int): String = UiPrefs.wrapLocale(this).getString(id)
    private fun modeNotification(text: String): String {
        val mode = uiString(AudioModes.mode(CleanerPrefs.sourcePreset(this)).label)
        val label = UiPrefs.wrapLocale(this).getString(R.string.notification_mode, mode)
        return if (text == uiString(R.string.notification_active)) label else "$label · $text"
    }

    private fun recordRecovery(metrics: String) {
        var fallbacks = lastFallbacks
        var recoveries = lastRecoveries
        var failures = lastProbeFailures
        var underruns = lastUnderruns
        counterPattern.findAll(metrics).forEach { match ->
            val value = match.groups["value"]!!.value.toLongOrNull() ?: 0
            when (match.groups["name"]!!.value) {
                "fallback_events" -> fallbacks = value
                "recoveries" -> recoveries = value
                "output_underruns" -> underruns = value
                else -> failures = value
            }
        }
        val now = SystemClock.elapsedRealtime()
        if (fallbacks > lastFallbacks || recoveries > lastRecoveries || failures > lastProbeFailures)
            CleanerPrefs.recordRecovery(this, false)
        val packets = packetPattern.find(metrics)?.groupValues?.get(1)?.toLongOrNull()
        if (packets != null) CleanerPrefs.setBufferingAdvice(this,
            playbackAdvice.observe(now, packets, fallbacks, underruns))
        healStarvedRoute(now, packets, underruns)
        lastUnderruns = underruns
        lastFallbacks = fallbacks; lastRecoveries = recoveries; lastProbeFailures = failures
    }

    /**
     * Self-healing: if the output keeps running dry while audio is flowing (underruns in
     * three health checks within 20 s, after the first seconds of a start), rebuild the
     * route once. At most once a minute, so a phone that is simply too busy is not
     * restarted in a loop; the buffering advice still appears for that case.
     */
    private fun healStarvedRoute(now: Long, packets: Long?, underruns: Long) {
        val flowing = packets != null && lastPackets >= 0 && packets > lastPackets
        if (packets != null) lastPackets = packets
        if (!flowing || switchingMode || now - routeStartedAt < 4_000) return
        if (underruns > lastUnderruns) underrunPolls.addLast(now)
        while (underrunPolls.isNotEmpty() && now - underrunPolls.first() > 20_000) underrunPolls.removeFirst()
        if (underrunPolls.size >= 3 && now - lastAutoRestart > 60_000) {
            lastAutoRestart = now
            Log.i("CleanWaveAudio", "Output kept running dry; rebuilding the route once")
            scheduleRestart(0)
        }
    }

    private var lastNotificationText: String? = null
    private fun updateNotification(text: String) {
        val signature = modeNotification(text)
        if (lastNotificationText == signature) return
        lastNotificationText = signature
        getSystemService(NotificationManager::class.java)
            .notify(NOTIFICATION_ID, notification(text))
    }
}
