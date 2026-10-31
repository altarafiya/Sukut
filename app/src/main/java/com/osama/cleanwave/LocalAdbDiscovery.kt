package com.osama.cleanwave

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Handler
import android.os.Looper
import java.io.Closeable
import java.net.NetworkInterface

/** On-demand discovery; other computers' ADB advertisements are ignored. */
internal class LocalAdbDiscovery(context: Context, private val found: (Boolean, Int) -> Unit) : Closeable {
    private val nsd = context.getSystemService(NsdManager::class.java)
    private val handler = Handler(Looper.getMainLooper())
    private val listeners = ArrayList<NsdManager.DiscoveryListener>()
    private val pending = ArrayDeque<Pair<Boolean, NsdServiceInfo>>()
    private var resolving = false
    private var closed = false
    fun start() { discover(true, "_adb-tls-pairing._tcp."); discover(false, "_adb-tls-connect._tcp.") }
    private fun discover(pairing: Boolean, type: String) {
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(type: String) {}
            override fun onDiscoveryStopped(type: String) {}
            override fun onStartDiscoveryFailed(type: String, code: Int) {}
            override fun onStopDiscoveryFailed(type: String, code: Int) {}
            override fun onServiceLost(info: NsdServiceInfo) {}
            override fun onServiceFound(info: NsdServiceInfo) { handler.post {
                if (!closed && pending.size < 16) { pending.add(pairing to info); resolveNext() }
            } }
        }
        listeners.add(listener)
        runCatching { nsd.discoverServices(type, NsdManager.PROTOCOL_DNS_SD, listener) }
    }
    @Suppress("DEPRECATION")
    private fun resolveNext() {
        if (closed || resolving || pending.isEmpty()) return
        val (pairing, info) = pending.removeFirst(); resolving = true
        nsd.resolveService(info, object : NsdManager.ResolveListener {
            override fun onResolveFailed(info: NsdServiceInfo, code: Int) { handler.post { resolving = false; resolveNext() } }
            override fun onServiceResolved(info: NsdServiceInfo) { handler.post {
                resolving = false
                if (!closed && AdbWire.validPort(info.port)) {
                    val host = info.host
                    val local = host != null && (host.isLoopbackAddress || NetworkInterface.getNetworkInterfaces().toList().any { it.inetAddresses.toList().any { address -> address == host } })
                    if (local) found(pairing, info.port)
                }
                resolveNext()
            } }
        })
    }
    override fun close() {
        closed = true; pending.clear(); handler.removeCallbacksAndMessages(null)
        listeners.forEach { runCatching { nsd.stopServiceDiscovery(it) } }; listeners.clear()
    }
}
