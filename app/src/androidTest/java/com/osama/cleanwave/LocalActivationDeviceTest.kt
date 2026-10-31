package com.osama.cleanwave

import android.content.ComponentName
import android.content.pm.ServiceInfo
import android.os.Build
import android.util.Base64
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.nio.ByteBuffer
import java.nio.ByteOrder

@RunWith(AndroidJUnit4::class)
class LocalActivationDeviceTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    @Test fun nativePairingAndKeystoreIdentityWorkOnThisDevice() {
        val first = LocalAdbIdentity().publicAdbKey()
        assertArrayEquals(first, LocalAdbIdentity().publicAdbKey())
        val raw = Base64.decode(first.toString(Charsets.UTF_8).substringBefore(' '), Base64.NO_WRAP)
        assertEquals(524, raw.size)
        assertEquals(64, ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN).int)
        assertEquals(65537, ByteBuffer.wrap(raw, 520, 4).order(ByteOrder.LITTLE_ENDIAN).int)
        val store = java.security.KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        val privateKey = store.getKey("SukutLocalAdbV2", null) as java.security.PrivateKey
        val signature = javax.crypto.Cipher.getInstance("RSA/ECB/NoPadding").run {
            init(javax.crypto.Cipher.ENCRYPT_MODE, privateKey)
            doFinal(ByteArray(256).apply { this[255] = 1 })
        }
        assertEquals(256, signature.size)
        val secret = ByteArray(70) { (it + 1).toByte() }
        val handle = AdbPairingCrypto.create(secret); secret.fill(0)
        assertTrue(handle != 0L)
        try {
            assertEquals(32, AdbPairingCrypto.message(handle).size)
            assertNull(AdbPairingCrypto.finish(handle, byteArrayOf(1)))
        } finally { AdbPairingCrypto.destroy(handle) }
    }
    @Test fun activationEntryPointsRemainPrivateAndBounded() {
        val manager = context.packageManager
        val activity = manager.getActivityInfo(ComponentName(context, LocalActivationActivity::class.java), 0)
        val service = manager.getServiceInfo(ComponentName(context, LocalActivationService::class.java), 0)
        assertFalse(activity.exported); assertFalse(service.exported)
        if (Build.VERSION.SDK_INT >= 34) assertEquals(ServiceInfo.FOREGROUND_SERVICE_TYPE_SHORT_SERVICE, service.foregroundServiceType)
    }
}
