package com.osama.cleanwave

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.net.Uri
import android.os.Binder
import android.os.Bundle
import android.os.Process

/** Only ADB shell can bootstrap; ordinary apps cannot send or retrieve a worker Binder. */
class WorkerProvider : ContentProvider() {
    override fun onCreate() = true
    override fun call(method: String, arg: String?, extras: Bundle?): Bundle {
        val caller = Binder.getCallingUid()
        if (!WorkerProtocol.trustedBootstrap(caller) && caller != Process.myUid()) throw SecurityException("ADB shell required")
        return when (method) {
            "attach" -> {
                if (!WorkerProtocol.trustedBootstrap(caller)) throw SecurityException("Only the ADB worker can attach")
                WorkerConnection.attach(requireNotNull(extras))
            }
            "status" -> Bundle().apply {
                putString("state", if (WorkerConnection.ready) "READY" else "INACTIVE")
                putInt("version", BuildConfig.VERSION_CODE)
                putInt("protocol", WorkerProtocol.VERSION)
                putInt("pid", WorkerConnection.workerPid)
            }
            "computer-setup" -> {
                if (!WorkerProtocol.trustedBootstrap(caller)) throw SecurityException("ADB shell required")
                check(WorkerConnection.ready) { "Activate the worker first" }
                val app = requireNotNull(context)
                check(DeveloperOptionsControl.available(app)) { "Computer shortcut permission was not granted" }
                DeveloperOptionsControl.computerReady(app)
                Bundle().apply { putString("state", "COMPUTER_SETUP_READY") }
            }
            "phone-setup" -> {
                if (!WorkerProtocol.trustedBootstrap(caller)) throw SecurityException("ADB shell required")
                check(WorkerConnection.ready) { "Activate the worker first" }
                val app = requireNotNull(context)
                check(DeveloperOptionsControl.available(app)) { "Shortcut permission was not granted" }
                DeveloperOptionsControl.phoneReady(app)
                Bundle().apply { putString("state", "PHONE_SETUP_READY") }
            }
            "deactivate" -> {
                val api = WorkerConnection.api
                // destroy exits after releasing the route; a dead reply is expected.
                runCatching { api?.destroy() }
                WorkerConnection.clear()
                Bundle().apply { putString("state", "INACTIVE") }
            }
            else -> throw IllegalArgumentException("Unknown worker operation")
        }
    }
    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, args: Array<out String>?, order: String?): Cursor? = null
    override fun getType(uri: Uri): String? = null
    override fun insert(uri: Uri, values: ContentValues?): Uri? = throw UnsupportedOperationException()
    override fun delete(uri: Uri, selection: String?, args: Array<out String>?) = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, args: Array<out String>?) = 0
}
