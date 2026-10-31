package com.osama.cleanwave

import android.content.Context
import android.os.Process
import org.lsposed.hiddenapibypass.HiddenApiBypass
import java.io.Closeable
import java.io.DataInputStream
import java.io.DataOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.nio.ByteBuffer
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/** No host/command input. Only loopback and the bootstrap shipped in this APK. */
internal class LocalAdb : Closeable {
    @Volatile private var socket: Socket? = null
    @Volatile private var cancelled = false
    var stage = "initialising"; private set
    private fun connect(port: Int): Socket {
        require(AdbWire.validPort(port)); check(!cancelled)
        val s = Socket(); socket = s
        s.soTimeout = 12000
        s.connect(InetSocketAddress("127.0.0.1", port), 4000)
        check(!cancelled); return s
    }
    fun pair(port: Int, code: String) {
        require(AdbWire.validCode(code)) { "Use the six-digit pairing code" }
        stage = "pairing identity"
        val identity = LocalAdbIdentity()
        stage = "pairing TLS"
        identity.tls(connect(port), port).use { tls ->
            socket = tls
            stage = "TLS exporter"
            HiddenApiBypass.addHiddenApiExemptions("Lcom/android/org/conscrypt/")
            val conscrypt = Class.forName("com.android.org.conscrypt.Conscrypt")
            val exporter = conscrypt.getDeclaredMethod("exportKeyingMaterial", javax.net.ssl.SSLSocket::class.java,
                String::class.java, ByteArray::class.java, Int::class.javaPrimitiveType).apply { isAccessible = true }
                .invoke(null, tls, "adb-label\u0000", null, 64) as ByteArray
            val password = code.toByteArray(Charsets.UTF_8) + exporter
            stage = "SPAKE2"
            val handle = AdbPairingCrypto.create(password)
            password.fill(0); exporter.fill(0)
            check(handle != 0L) { "Pairing could not initialise" }
            var key: ByteArray? = null
            try {
                val input = DataInputStream(tls.inputStream); val output = DataOutputStream(tls.outputStream)
                fun send(type: Int, payload: ByteArray) { output.writeByte(1); output.writeByte(type); output.writeInt(payload.size); output.write(payload); output.flush() }
                fun receive(type: Int, max: Int): ByteArray {
                    require(input.readUnsignedByte() == 1 && input.readUnsignedByte() == type) { "Invalid pairing response" }
                    val size = input.readInt(); require(size in 1..max) { "Invalid pairing response size" }
                    return ByteArray(size).also { input.readFully(it) }
                }
                send(0, AdbPairingCrypto.message(handle))
                key = AdbPairingCrypto.finish(handle, receive(0, 32)) ?: error("Pairing code was not accepted")
                stage = "peer authentication"
                fun crypt(mode: Int, data: ByteArray): ByteArray = Cipher.getInstance("AES/GCM/NoPadding").run {
                    init(mode, SecretKeySpec(key, "AES"), GCMParameterSpec(128, ByteArray(12))); doFinal(data)
                }
                val info = ByteArray(8192); info[0] = 0
                val pub = identity.publicAdbKey(); pub.copyInto(info, 1)
                send(1, crypt(Cipher.ENCRYPT_MODE, info))
                val peer = crypt(Cipher.DECRYPT_MODE, receive(1, 8208))
                require(peer.size == 8192 && (peer[0].toInt() == 0 || peer[0].toInt() == 1)) { "Invalid pairing identity" }
            } finally { key?.fill(0); AdbPairingCrypto.destroy(handle) }
        }
    }
    fun activate(context: Context, port: Int) {
        stage = "ADB connect"
        var connection = connect(port)
        var input = DataInputStream(connection.getInputStream()); var output = connection.getOutputStream()
        AdbWire.write(output, AdbWire.CNXN, 0x01000001, AdbWire.MAX, "host::\u0000".toByteArray())
        var tls = false; var connected = false
        repeat(8) {
            if (!connected) {
                val packet = AdbWire.read(input)
                when (packet.command) {
                    AdbWire.STLS -> {
                        stage = "ADB TLS"
                        check(!tls); AdbWire.write(output, AdbWire.STLS, 0x01000000)
                        connection = LocalAdbIdentity().tls(connection, port); socket = connection
                        input = DataInputStream(connection.getInputStream()); output = connection.getOutputStream(); tls = true
                    }
                    AdbWire.CNXN -> { check(tls) { "Wireless debugging requires pairing" }; connected = true }
                    AdbWire.AUTH -> error("Pair Sukut with Wireless debugging first")
                    else -> error("Unexpected local ADB handshake")
                }
            }
        }
        check(connected) { "Wireless debugging did not connect" }
        stage = "worker bootstrap"
        val user = Process.myUid() / 100000
        val script = context.assets.open("activate-worker.sh").bufferedReader().use { it.readText() }
        // shell: service executes the immutable APK asset, never user-provided text.
        // Explicit raw shell avoids a PTY closing underneath the detached child.
        // Keep its launcher alive until app_process has actually started; a bare
        // background fork can otherwise receive the shell's hangup before setsid.
        val settle = "\nfor sukut_try in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50; do [ -s /data/local/tmp/sukut-altarafiyah-worker-u$user.log ] && break; sleep 0.1; done\n"
        val command = "shell,raw:set -- $user; : > /data/local/tmp/sukut-altarafiyah-worker-u$user.log;\n$script\n$settle"
        AdbWire.write(output, AdbWire.OPEN, 1, 0, (command + '\u0000').toByteArray())
        var remote = 0; var total = 0; var closed = false
        val response = java.io.ByteArrayOutputStream()
        while (!closed && !cancelled) {
            val packet = AdbWire.read(input)
            require(packet.arg1 == 1) { "Unexpected ADB stream" }
            when (packet.command) {
                AdbWire.OKAY -> remote = packet.arg0
                AdbWire.WRTE -> {
                    require(remote == 0 || remote == packet.arg0); remote = packet.arg0
                    total += packet.data.size; require(total <= 65536) { "Activation output too large" }
                    response.write(packet.data)
                    AdbWire.write(output, AdbWire.OKAY, 1, remote)
                }
                AdbWire.CLSE -> { AdbWire.write(output, AdbWire.CLSE, 1, packet.arg0); closed = true }
                else -> error("Unexpected ADB stream packet")
            }
        }
        check(!cancelled)
        check(response.toString("UTF-8").contains("SUKUT_WORKER_LAUNCHED user=$user")) { "Installed worker bootstrap did not complete" }
        stage = "worker Binder"
        // Success is determined by the authenticated Binder, never shell text.
        repeat(40) { if (WorkerConnection.ready) return; Thread.sleep(100) }
        error("Worker did not connect. Keep Sukut open and try activation again.")
    }
    override fun close() { cancelled = true; runCatching { socket?.close() }; socket = null }
}

