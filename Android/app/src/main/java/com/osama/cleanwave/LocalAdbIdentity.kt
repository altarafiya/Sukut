package com.osama.cleanwave

import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.math.BigInteger
import java.net.Socket
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.Principal
import java.security.PrivateKey
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.security.interfaces.RSAPublicKey
import java.util.Date
import javax.net.ssl.*
import javax.security.auth.x500.X500Principal

/** A per-install, non-exportable pairing identity, unrelated to APK signing. */
internal class LocalAdbIdentity {
    private val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    init {
        if (!store.containsAlias(ALIAS)) KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore").apply {
            initialize(KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                .setKeySize(2048).setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256, KeyProperties.DIGEST_SHA384, KeyProperties.DIGEST_SHA512)
                .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1, KeyProperties.SIGNATURE_PADDING_RSA_PSS)
                // Platform Conscrypt encodes TLS1.3 PSS, then delegates the raw
                // RSA signature via Cipher.ENCRYPT_MODE/NoPadding to Keystore.
                // The key still has SIGN/VERIFY purposes only, never decryption.
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setCertificateSubject(X500Principal("CN=Sukut local activation"))
                .setCertificateSerialNumber(BigInteger.ONE).setCertificateNotBefore(Date(0))
                .setCertificateNotAfter(Date(4102444800000L)).build())
            generateKeyPair()
        }
    }
    private val key = store.getKey(ALIAS, null) as PrivateKey
    private val certificate = store.getCertificate(ALIAS) as X509Certificate
    fun tls(socket: Socket, port: Int): SSLSocket {
        require(socket.inetAddress.isLoopbackAddress)
        val keys = object : X509ExtendedKeyManager() {
            override fun chooseClientAlias(types: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?) = ALIAS
            override fun getCertificateChain(alias: String?) = arrayOf(certificate)
            override fun getPrivateKey(alias: String?) = key
            override fun getClientAliases(type: String?, issuers: Array<out Principal>?) = arrayOf(ALIAS)
            override fun getServerAliases(type: String?, issuers: Array<out Principal>?): Array<String>? = null
            override fun chooseServerAlias(type: String?, issuers: Array<out Principal>?, socket: Socket?): String? = null
        }
        // ADB uses self-signed certificates. Pairing authenticates with SPAKE2,
        // and every socket is hard-coded to this phone's loopback interface.
        val trust = object : X509TrustManager {
            override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, type: String?) {}
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, type: String?) {}
        }
        val context = SSLContext.getInstance("TLSv1.3").apply { init(arrayOf(keys), arrayOf(trust), SecureRandom()) }
        return (context.socketFactory.createSocket(socket, "127.0.0.1", port, true) as SSLSocket).apply {
            enabledProtocols = arrayOf("TLSv1.3"); soTimeout = 12000; startHandshake()
        }
    }
    fun publicAdbKey(): ByteArray {
        val public = certificate.publicKey as RSAPublicKey
        val n = public.modulus
        fun little(value: BigInteger, size: Int): ByteArray {
            val bytes = value.toByteArray().reversedArray()
            return ByteArray(size) { if (it < bytes.size) bytes[it] else 0 }
        }
        val raw = ByteBuffer.allocate(524).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(64).putInt(n.modInverse(BigInteger.ONE.shiftLeft(32)).negate().toInt())
            .put(little(n, 256)).put(little(BigInteger.ONE.shiftLeft(4096).mod(n), 256))
            .putInt(public.publicExponent.toInt()).array()
        return (Base64.encodeToString(raw, Base64.NO_WRAP) + " Sukut\u0000").toByteArray(Charsets.UTF_8)
    }
    companion object { private const val ALIAS = "SukutLocalAdbV2" }
}
