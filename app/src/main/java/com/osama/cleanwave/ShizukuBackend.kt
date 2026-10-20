package com.osama.cleanwave

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.IBinder
import android.os.Handler
import android.os.Looper
import rikka.shizuku.Shizuku
import java.util.concurrent.Executors

class ShizukuBackend(private val context: Context, private val listener: Listener) {
    interface Listener {
        fun onStarted(detail: String)
        fun onFailed(reason: String)
        fun onStopped()
        fun onProgress(detail: String, metrics: String)
        fun onOutputRoute(deviceId: Int, deviceType: Int) = Unit
    }
    companion object {
        enum class Availability {
            NOT_RUNNING,
            PERMISSION_NEEDED,
            READY_SHELL,
            READY_ROOT,
            READY_OTHER,
            UNAVAILABLE,
        }

        fun availabilityState(): Availability = try {
            when {
                !Shizuku.pingBinder() -> Availability.NOT_RUNNING
                Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED -> Availability.PERMISSION_NEEDED
                Shizuku.getUid() == 2000 -> Availability.READY_SHELL
                Shizuku.getUid() == 0 -> Availability.READY_ROOT
                else -> Availability.READY_OTHER
            }
        } catch (_: Throwable) {
            Availability.UNAVAILABLE
        }

        fun availability(): String = when (availabilityState()) {
            Availability.NOT_RUNNING -> "Shizuku is not running"
            Availability.PERMISSION_NEEDED -> "Shizuku permission not granted"
            Availability.READY_SHELL -> "Shizuku ready (ADB/shell mode)"
            Availability.READY_ROOT -> "Shizuku ready (root mode)"
            Availability.READY_OTHER -> "Shizuku ready"
            Availability.UNAVAILABLE -> "Shizuku unavailable"
        }

        /** OFF also cleans a worker left behind by a previous controller process.
         * peekUserService explicitly forbids creating a new audio service. */
        fun stopExisting(context: Context, completed: () -> Unit) {
            val cleanup = ShizukuBackend(context.applicationContext, object : Listener {
                override fun onStarted(detail: String) = Unit
                override fun onFailed(reason: String) = Unit
                override fun onStopped() = Unit
                override fun onProgress(detail: String, metrics: String) = Unit
            })
            cleanup.peekAndStop(completed)
        }
    }

    private val args = Shizuku.UserServiceArgs(ComponentName(context, PrivilegedAudioService::class.java))
        .processNameSuffix("cleanwave_audio").tag("cleanwave-system-audio").version(BuildConfig.VERSION_CODE).daemon(false)
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
    private val binderDeadListener = Shizuku.OnBinderDeadListener {
        handler.post {
            if (!stopping && bindingRequested) fail("Shizuku stopped. Start Shizuku again before enabling Sukut.")
        }
    }
    private val bindTimeout = Runnable {
        if (!bound && !stopping) {
            if (cleanupOnly) stop(afterStopped = cleanupDone)
            else fail("Shizuku audio service did not connect within 10 seconds. Use tested Shizuku 13.5.4 on this Xiaomi.")
        }
    }
    private val startTimeout = Runnable {
        if (!stopping) fail("Audio model initialization timed out. Turn Off and retry.")
    }
    private val health = object : Runnable {
        override fun run() {
            val api = remote ?: return
            if (!bound || stopping) return
            val request = epoch
            io.execute {
                val result = runCatching {
                    check(api.isRunning) { api.status }
                    val metrics = api.processingMetrics
                    api.status to metrics
                }
                handler.post {
                    if (stopping || request != epoch) return@post
                    result.onSuccess { (detail, metrics) ->
                        listener.onProgress(detail, metrics)
                        handler.postDelayed(this, if (SukutApplication.uiVisible) 750 else 3000)
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
            check(Shizuku.pingBinder()) { "Shizuku is not running. Start Shizuku first." }
            check(Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) { "Grant Sukut Shizuku access first." }
            bindingRequested = true; epoch++
            Shizuku.addBinderDeadListener(binderDeadListener); deathRegistered = true
            handler.postDelayed(bindTimeout, 10000)
            Shizuku.bindUserService(args, connection)
        } catch (t: Throwable) { fail(t.message ?: t.javaClass.simpleName) }
    }

    private fun peekAndStop(completed: () -> Unit) {
        cleanupOnly = true; cleanupDone = completed
        try {
            if (!Shizuku.pingBinder() || Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) {
                stop(afterStopped = completed); return
            }
            bindingRequested = true
            handler.postDelayed(bindTimeout, 3000)
            if (Shizuku.peekUserService(args, connection) < 0) stop(afterStopped = completed)
        } catch (_: Throwable) { stop(afterStopped = completed) }
    }

    private fun fail(reason: String) {
        if (stopping) return
        stop()
        listener.onFailed(reason)
    }

    fun stop(removeService: Boolean = true, afterStopped: (() -> Unit)? = null) {
        afterStopped?.let { stoppedCallbacks.add(it) }
        if (stopQueued) return
        stopQueued = true; stopping = true; epoch++
        handler.removeCallbacks(bindTimeout); handler.removeCallbacks(startTimeout); handler.removeCallbacks(health)
        if (deathRegistered) { Shizuku.removeBinderDeadListener(binderDeadListener); deathRegistered = false }
        val api = remote; remote = null
        val unbind = bindingRequested
        bound = false; bindingRequested = false
        io.execute {
            runCatching { api?.stopProcessing() }
            if (unbind) {
                val success = runCatching { Shizuku.unbindUserService(args, connection, removeService) }.isSuccess
                if (removeService && !success) runCatching { api?.destroy() }
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
     * before Shizuku has finished killing it. IO ordering also makes OFF cancel
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
