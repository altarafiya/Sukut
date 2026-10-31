package com.osama.cleanwave

import android.os.Binder
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import java.util.concurrent.CopyOnWriteArraySet

/** One cached Binder and one process lease; no idle timer, socket or audio buffers. */
object WorkerConnection {
    fun interface OnChanged { fun changed() }
    private val listeners = CopyOnWriteArraySet<OnChanged>()
    private val main by lazy { Handler(Looper.getMainLooper()) }
    private val lease = Binder()
    @Volatile private var binder: IBinder? = null
    @Volatile var workerPid = 0
        private set
    val ready: Boolean get() = binder?.isBinderAlive == true
    val api: ICleanWaveService? get() = binder?.takeIf { it.isBinderAlive }?.let(ICleanWaveService.Stub::asInterface)
    @Synchronized fun attach(extras: Bundle): Bundle {
        val answer = Bundle()
        if (!WorkerProtocol.compatible(extras.getInt("protocol"), extras.getInt("version"))) {
            answer.putString("reason", "version"); return answer
        }
        val incoming = extras.getBinder("worker") ?: return answer
        if (!incoming.isBinderAlive) return answer
        if (binder !== incoming) {
            incoming.linkToDeath({ clear(incoming) }, 0)
            binder = incoming
        }
        workerPid = extras.getInt("pid")
        answer.putBoolean("accepted", true)
        answer.putInt("protocol", WorkerProtocol.VERSION)
        answer.putInt("version", BuildConfig.VERSION_CODE)
        answer.putBinder("lease", lease)
        notifyChanged()
        return answer
    }
    @Synchronized fun clear(expected: IBinder? = binder) {
        if (binder !== expected) return
        binder = null; workerPid = 0; notifyChanged()
    }
    fun addListener(listener: OnChanged) { listeners.add(listener); main.post { listener.changed() } }
    fun removeListener(listener: OnChanged) { listeners.remove(listener) }
    private fun notifyChanged() { main.post { listeners.forEach { it.changed() } } }
}
