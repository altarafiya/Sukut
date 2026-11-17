package com.osama.cleanwave

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors

class WorkerBackend(private val context: Context, private val listener: Listener) {
    interface Listener {
        fun onStarted(detail: String)
        fun onFailed(reason: String)
        fun onStopped()
        fun onProgress(detail: String, metrics: String)
        fun onOutputRoute(deviceId: Int, deviceType: Int) = Unit
    }
    companion object {
        enum class Availability { NOT_RUNNING, READY, UNAVAILABLE }
        fun availabilityState(): Availability = if (WorkerConnection.ready) Availability.READY else Availability.NOT_RUNNING
        fun availability(): String = if (WorkerConnection.ready) "Sukut worker ready" else "Activate Sukut from Windows"

        /** OFF only stops the attached worker; it never launches a process. */
        fun stopExisting(context: Context, completed: () -> Unit) {
            val cleanup = WorkerBackend(context.applicationContext, object : Listener {
                override fun onStarted(detail: String) = Unit
                override fun onFailed(reason: String) = Unit
                override fun onStopped() = Unit
                override fun onProgress(detail: String, metrics: String) = Unit
            })
            cleanup.peekAndStop(completed)
        }
    }

    private val handler = Handler(Looper.getMainLooper())
    private val io = Executors.newSingleThreadExecutor { task -> Thread(task, "CleanWave-Control").apply { isDaemon = true } }
    @Volatile private var remote: ICleanWaveService? = null
    @Volatile private var stopping = false
    @Volatile private var epoch = 0L
    @Volatile private var configuration = 0L
    private var bound = false
    private var bindingRequested = false
    private var deathRegistered = false
    private var stopQueued = false
    private var cleanupOnly = false
    private var cleanupDone: (() -> Unit)? = null
    private var sentReduction = Int.MIN_VALUE
    private var sentBoost = Int.MIN_VALUE
    private var sentVoiceDb = Int.MIN_VALUE
    private var sentMusicDb = Int.MIN_VALUE
    private var sentOtherDb = Int.MIN_VALUE
    private val stoppedCallbacks = mutableListOf<() -> Unit>()
    private fun routeListener(request: Long) = object : IOutputRouteListener.Stub() {
        override fun onOutputRouteChanged(deviceId: Int, deviceType: Int) {
            handler.post {
                if (!stopping && request == epoch) listener.onOutputRoute(deviceId, deviceType)
            }
        }
    }
    private val binderDeadListener = IBinder.DeathRecipient {
        handler.post { if (!stopping && bindingRequested) fail("Sukut worker stopped. Activate it again from Windows → Phone.") }
    }
    private val bindTimeout = Runnable {
        if (!bound && !stopping) {
            if (cleanupOnly) stop(afterStopped = cleanupDone)
            else fail("Sukut worker did not connect. Activate it from Windows → Phone.")
        }
    }
    private val startTimeout = Runnable {
        if (!stopping) fail("Sukut model initialization timed out. Turn Off and retry.")
    }
    private val health = object : Runnable {
        override fun run() {
            val api = remote ?: return
            if (!bound || stopping) return
            val request = epoch
            io.execute {
                val result = runCatching {
                    val health = EngineHealth.parse(api.healthSnapshot)
                    check(health.running) { health.detail }
                    health.detail to health.metrics
                }
                handler.post {
                    if (stopping || request != epoch) return@post
                    result.onSuccess { (detail, metrics) ->
                        listener.onProgress(detail, metrics)
                        // Binder death and output-route changes have their own callbacks.
                        // This is a health watchdog, not an audio processing clock.
                        handler.postDelayed(this, ControlPlanePolicy.healthInterval(SukutApplication.uiVisible))
                    }.onFailure { fail("Audio route stopped: ${it.message ?: it.javaClass.simpleName}") }
                }
            }
        }
    }
    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, service: IBinder?) {
            if (service == null || stopping) return
            handler.removeCallbacks(bindTimeout)
            val api = ICleanWaveService.Stub.asInterface(service)
            remote = api; bound = true
            if (cleanupOnly) { stop(afterStopped = cleanupDone); return }
            val request = epoch
            val mode = CleanerPrefs.processingMode(context)
            val reduction = CleanerPrefs.musicReduction(context)
            val boost = CleanerPrefs.voiceBoost(context)
            val voiceDb = CleanerPrefs.sourceVoiceDb(context)
            val musicDb = CleanerPrefs.sourceMusicDb(context)
            val otherDb = CleanerPrefs.sourceOtherDb(context)
            val buffering = CleanerPrefs.buffering(context)
            val amplification = CleanerPrefs.amplification(context)
            val silent = CleanerPrefs.silentRecovery(context)
            val effects = CleanerPrefs.effectsCompatibility(context)
            val sourceUid = CleanerPrefs.sourceUid(context)
            handler.postDelayed(startTimeout, 30000)
            io.execute {
                val result = runCatching {
                    check(!stopping && request == epoch) { "Start cancelled" }
                    api.setProcessingMode(mode); api.setMusicReduction(reduction); api.setVoiceBoost(boost)
                    api.setSourceMix(voiceDb, musicDb, otherDb)
                    api.setPlaybackOptions(buffering, amplification, silent, effects, sourceUid)
                    api.setOutputRouteListener(routeListener(request))
                    sentReduction = reduction; sentBoost = boost
                    sentVoiceDb = voiceDb; sentMusicDb = musicDb; sentOtherDb = otherDb
                    check(!stopping && request == epoch) { "Start cancelled" }
                    check(api.startPassthrough()) { api.status }
                    "${api.status} • ${api.identity}"
                }
                handler.post {
                    if (stopping || request != epoch) return@post
                    handler.removeCallbacks(startTimeout)
                    result.onSuccess { listener.onStarted(it); handler.postDelayed(health, 1000) }
                        .onFailure { fail(it.message ?: it.javaClass.simpleName) }
                }
            }
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            bound = false
            if (!stopping) fail("Privileged audio service disconnected")
            else remote = null
        }
    }

    fun start() {
        try {
            val service = WorkerConnection.api?.asBinder()
                ?: error("Activate Sukut from Windows → Phone before pressing Start.")
            bindingRequested = true; epoch++
            service.linkToDeath(binderDeadListener, 0); deathRegistered = true
            connection.onServiceConnected(null, service)
        } catch (t: Throwable) { fail(t.message ?: t.javaClass.simpleName) }
    }

    private fun peekAndStop(completed: () -> Unit) {
        remote = WorkerConnection.api
        stop(afterStopped = completed)
    }

    private fun fail(reason: String) {
        if (stopping) return
        stop()
        listener.onFailed(reason)
    }

    fun stop(afterStopped: (() -> Unit)? = null) {
        afterStopped?.let { stoppedCallbacks.add(it) }
        if (stopQueued) return
        stopQueued = true; stopping = true; epoch++
        handler.removeCallbacks(bindTimeout); handler.removeCallbacks(startTimeout); handler.removeCallbacks(health)

        val api = remote; remote = null
        bound = false; bindingRequested = false
        io.execute {
            runCatching { api?.stopProcessing() }
            if (deathRegistered) {
                runCatching { api?.asBinder()?.unlinkToDeath(binderDeadListener, 0) }
                deathRegistered = false
            }
            handler.post {
                listener.onStopped()
                val callbacks = stoppedCallbacks.toList(); stoppedCallbacks.clear()
                callbacks.forEach { it() }
            }
            io.shutdown()
        }
    }

    fun updateMusicReduction() {
        val api = remote ?: return
        if (stopping) return
        val request = ++configuration
        val reduction = CleanerPrefs.musicReduction(context); val boost = CleanerPrefs.voiceBoost(context)
        val voiceDb = CleanerPrefs.sourceVoiceDb(context)
        val musicDb = CleanerPrefs.sourceMusicDb(context)
        val otherDb = CleanerPrefs.sourceOtherDb(context)
        val buffering = CleanerPrefs.buffering(context)
        val amplification = CleanerPrefs.amplification(context)
        val silent = CleanerPrefs.silentRecovery(context)
        val effects = CleanerPrefs.effectsCompatibility(context)
        val sourceUid = CleanerPrefs.sourceUid(context)
        io.execute {
            if (stopping || request != configuration) return@execute
            runCatching {
                api.setPlaybackOptions(buffering, amplification, silent, effects, sourceUid)
                if (reduction != sentReduction) {
                    api.setMusicReduction(reduction); sentReduction = reduction
                }
                if (boost != sentBoost) {
                    api.setVoiceBoost(boost); sentBoost = boost
                }
                if (voiceDb != sentVoiceDb || musicDb != sentMusicDb || otherDb != sentOtherDb) {
                    api.setSourceMix(voiceDb, musicDb, otherDb)
                    sentVoiceDb = voiceDb; sentMusicDb = musicDb; sentOtherDb = otherDb
                }
            }
                .onFailure { problem -> handler.post { if (!stopping) fail("Could not update cleanup: ${problem.message}") } }
        }
    }

    /** Rebuild the model/route without destroying a Binder that may be re-bound
     * while its control connection remains activated. IO ordering also makes OFF cancel
     * both an in-flight rebuild and any superseded configuration request. */
    fun restartRoute() {
        if (stopping) return
        val request = ++epoch
        configuration++
        handler.removeCallbacks(health)
        handler.removeCallbacks(startTimeout)
        val api = remote ?: return // A pending connection reads the latest preferences.
        handler.postDelayed(startTimeout, 30000)
        io.execute {
            if (stopping || request != epoch) return@execute
            val result = runCatching {
                api.stopForRestart()
                check(!stopping && request == epoch) { "Restart superseded" }
                val reduction = CleanerPrefs.musicReduction(context)
                val boost = CleanerPrefs.voiceBoost(context)
                val voice = CleanerPrefs.sourceVoiceDb(context)
                val music = CleanerPrefs.sourceMusicDb(context)
                val other = CleanerPrefs.sourceOtherDb(context)
                api.setProcessingMode(CleanerPrefs.processingMode(context))
                api.setMusicReduction(reduction); api.setVoiceBoost(boost)
                api.setSourceMix(voice, music, other)
                api.setPlaybackOptions(CleanerPrefs.buffering(context), CleanerPrefs.amplification(context),
                    CleanerPrefs.silentRecovery(context), CleanerPrefs.effectsCompatibility(context), CleanerPrefs.sourceUid(context))
                api.setOutputRouteListener(routeListener(request))
                sentReduction = reduction; sentBoost = boost
                sentVoiceDb = voice; sentMusicDb = music; sentOtherDb = other
                check(!stopping && request == epoch) { "Restart superseded" }
                check(api.startPassthrough()) { api.status }
                "${api.status} • ${api.identity}"
            }
            handler.post {
                if (stopping || request != epoch) return@post
                handler.removeCallbacks(startTimeout)
                result.onSuccess { listener.onStarted(it); handler.postDelayed(health, 1000) }
                    .onFailure { fail("Could not restart audio: ${it.message}") }
            }
        }
    }
}
