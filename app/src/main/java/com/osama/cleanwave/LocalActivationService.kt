package com.osama.cleanwave

import android.app.*
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.*
import java.util.concurrent.Executors

/** Exists for at most 150 seconds during an explicit activation/pairing session. */
class LocalActivationService : Service() {
    override fun attachBaseContext(base: android.content.Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    private val main = Handler(Looper.getMainLooper())
    private val executor = Executors.newSingleThreadExecutor()
    private var discovery: LocalAdbDiscovery? = null
    private var session: LocalAdb? = null
    private var pairingPort = 0
    private var connectPort = 0
    private var code: String? = null
    private var paired = false
    private var working = false
    private var preferPair = false
    private var destroyed = false
    private var connectRetries = 0
    private val timeout = Runnable { finish(R.string.local_timeout) }
    override fun onBind(intent: Intent?) = null
    @android.annotation.SuppressLint("ForegroundServiceType") // AGP8.7 false positive with compileSdk36; merged manifest + device test verify shortService.
    override fun onCreate() {
        super.onCreate()
        val manager = getSystemService(NotificationManager::class.java)
        manager.createNotificationChannel(NotificationChannel(CHANNEL, getString(R.string.local_title), NotificationManager.IMPORTANCE_DEFAULT))
        val note = notification(R.string.local_searching)
        if (Build.VERSION.SDK_INT >= 34) startForeground(ID, note, ServiceInfo.FOREGROUND_SERVICE_TYPE_SHORT_SERVICE)
        else startForeground(ID, note)
        main.postDelayed(timeout, 150000)
    }
    override fun onStartCommand(intent: Intent?, flags: Int, id: Int): Int {
        if (intent?.action == CANCEL) { finish(R.string.local_cancelled); return START_NOT_STICKY }
        val reply = intent?.let { RemoteInput.getResultsFromIntent(it) }?.getCharSequence(CODE)?.toString()?.trim()
        val supplied = reply ?: intent?.getStringExtra(CODE)
        if (supplied != null) {
            if (!AdbWire.validCode(supplied)) { update(R.string.local_bad_code); return START_NOT_STICKY }
            code = supplied; preferPair = true
        }
        if (intent?.action == PAIR) preferPair = true
        if (intent?.action == ACTIVATE && !working) preferPair = false
        intent?.getIntExtra("pair_port", 0)?.takeIf(AdbWire::validPort)?.let { pairingPort = it }
        intent?.getIntExtra("connect_port", 0)?.takeIf(AdbWire::validPort)?.let { connectPort = it }
        if (WorkerConnection.ready && !preferPair) { finish(R.string.local_ready); return START_NOT_STICKY }
        discover()
        attempt(); return START_NOT_STICKY
    }
    private fun discover() {
        if (destroyed || discovery != null) return
        update(if (preferPair) R.string.local_pair_wait else R.string.local_searching)
        discovery = LocalAdbDiscovery(this) { pairing, port ->
            if (pairing) pairingPort = port else connectPort = port
            attempt()
        }.also { it.start() }
    }
    private fun attempt() {
        if (working) return
        val pairing = preferPair && !paired
        if (pairing && (pairingPort == 0 || code == null)) { update(R.string.local_pair_wait); return }
        if (!pairing && connectPort == 0) return
        working = true
        val entered = code; code = null
        update(if (pairing) R.string.local_pairing else R.string.local_activating)
        val client = LocalAdb(); session = client
        executor.execute {
            val result = runCatching { if (pairing) client.pair(pairingPort, entered!!) else client.activate(this, connectPort) }
            client.close()
            main.post {
                if (destroyed) return@post
                working = false; session = null
                if (result.isSuccess) {
                    if (pairing) { paired = true; preferPair = false; update(R.string.local_paired); attempt() }
                    else finish(R.string.local_ready)
                } else {
                    // Never log a six-digit code or TLS/key material.
                    val failure = result.exceptionOrNull()
                    android.util.Log.w("SukutLocalActivation", "${client.stage}: ${failure?.javaClass?.simpleName}/${failure?.cause?.javaClass?.simpleName}")
                    if (pairing) { code = null; update(R.string.local_pair_failed) }
                    else if(preferPair) { update(R.string.local_pair_wait); attempt() }
                    else if(client.stage == "ADB connect" && failure is java.net.ConnectException && connectRetries < 2) {
                        // Android can replace its listening port while the old NSD
                        // record is still cached. Keep the paired identity, discard
                        // this session's ports and resolve afresh within its deadline.
                        connectRetries++; connectPort=0; pairingPort=0
                        discovery?.close(); discovery=null
                        update(R.string.local_searching)
                        main.postDelayed({ discover() },300)
                    } else finish(R.string.local_activate_failed)
                }
            }
        }
    }
    private fun notification(text: Int): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, ReactivationActivity::class.java), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        val reply = PendingIntent.getService(this, 1, Intent(this, LocalActivationService::class.java).setAction(PAIR), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE)
        val cancel = PendingIntent.getService(this, 2, Intent(this, LocalActivationService::class.java).setAction(CANCEL), PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        return Notification.Builder(this, CHANNEL).apply {
            if(Build.VERSION.SDK_INT>=31)setForegroundServiceBehavior(Notification.FOREGROUND_SERVICE_IMMEDIATE)
        }.setSmallIcon(R.drawable.ic_sukut_tile)
            .setContentTitle(getString(R.string.local_title)).setContentText(getString(text))
            .setStyle(Notification.BigTextStyle().bigText(getString(text))).setContentIntent(open)
            .setOnlyAlertOnce(true).setOngoing(true)
            .addAction(Notification.Action.Builder(null, getString(R.string.local_enter_code), reply)
                .addRemoteInput(RemoteInput.Builder(CODE).setLabel(getString(R.string.local_code)).build()).build())
            .addAction(Notification.Action.Builder(null, getString(R.string.cancel), cancel).build()).build()
    }
    private fun update(text: Int) {
        getSystemService(NotificationManager::class.java).notify(ID, notification(text))
        LocalActivationState.update(text, true, posted = true)
    }
    private fun finish(text: Int) { LocalActivationState.update(text, false); stopSelf() }
    override fun onTimeout(startId: Int) { finish(R.string.local_timeout) }
    override fun onTimeout(startId: Int, fgsType: Int) { finish(R.string.local_timeout) }
    override fun onDestroy() {
        destroyed = true
        code = null; main.removeCallbacksAndMessages(null)
        discovery?.close(); discovery = null; session?.close(); session = null
        executor.shutdownNow(); stopForeground(STOP_FOREGROUND_REMOVE)
        if (LocalActivationState.busy) LocalActivationState.update(R.string.local_cancelled, false)
        super.onDestroy()
    }
    companion object {
        const val PAIR = BuildConfig.APPLICATION_ID + ".LOCAL_PAIR"
        const val ACTIVATE = BuildConfig.APPLICATION_ID + ".LOCAL_ACTIVATE"
        const val CANCEL = BuildConfig.APPLICATION_ID + ".LOCAL_CANCEL"
        const val CODE = "pairing_code"
        const val CHANNEL = "local_activation"
        private const val ID = 41
    }
}

