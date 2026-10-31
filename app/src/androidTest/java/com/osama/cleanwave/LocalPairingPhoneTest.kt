package com.osama.cleanwave

import android.content.Intent
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/** Explicit opt-in only. Exercises this phone's OS pairing dialog, never starts audio. */
@RunWith(AndroidJUnit4::class)
class LocalPairingPhoneTest {
    @Test fun pairAndActivateUsingThisPhonesAndroidDialog() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("localPairing") == "true")
        val ins = InstrumentationRegistry.getInstrumentation()
        val context = ins.targetContext
        assertFalse("Run with audio Off", CleanerPrefs.isEnabled(context))
        val ports = LinkedBlockingQueue<Pair<Boolean, Int>>()
        lateinit var discovery: LocalAdbDiscovery
        ins.runOnMainSync { discovery = LocalAdbDiscovery(context) { pairing, port -> ports.offer(pairing to port) }; discovery.start() }
        var client: LocalAdb? = null
        var activationPage: android.app.Activity? = null
        val throughService = InstrumentationRegistry.getArguments().getString("throughService") == "true"
        try {
            activationPage = ins.startActivitySync(Intent(context, LocalActivationActivity::class.java).putExtra("method",0).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            if (throughService) {
                val reconnectDeadline = SystemClock.elapsedRealtime() + 2000
                while (!WorkerConnection.ready && SystemClock.elapsedRealtime() < reconnectDeadline) SystemClock.sleep(50)
                if (WorkerConnection.ready) {
                    context.contentResolver.call(android.net.Uri.parse("content://com.altarafiyah.sukut.worker"), "deactivate", null, null)
                    SystemClock.sleep(200)
                }
                context.startForegroundService(Intent(context, LocalActivationService::class.java).setAction(LocalActivationService.PAIR))
                SystemClock.sleep(350)
            }
            ins.runOnMainSync { AudioTools.openWirelessDebugging(requireNotNull(activationPage)) }
            SystemClock.sleep(900)
            fun nodes(node: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> = buildList {
                if (node != null) { add(node); for (i in 0 until node.childCount) addAll(nodes(node.getChild(i))) }
            }
            var all = nodes(ins.uiAutomation.rootInActiveWindow)
            val settingsDeadline=SystemClock.elapsedRealtime()+5000
            fun settingsReady(list:List<AccessibilityNodeInfo>)=list.any { n ->
                val text=n.text?.toString().orEmpty()
                text.contains("pairing code",true)||text.contains("Wireless debugging",true)||text.contains("رمز الإقران")||text.contains("التصحيح اللاسلكي")
            }
            while(!settingsReady(all) && SystemClock.elapsedRealtime()<settingsDeadline) {
                SystemClock.sleep(100); all=nodes(ins.uiAutomation.rootInActiveWindow)
            }
            fun click(words: List<String>): Boolean {
                repeat(14) {
                    val visible = nodes(ins.uiAutomation.rootInActiveWindow)
                    var button = visible.firstOrNull { node -> words.any { word -> node.text?.toString()?.contains(word, true) == true } }
                    if (button != null) {
                        while (button != null && !button!!.isClickable) button = button!!.parent
                        if (button?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true) { SystemClock.sleep(650); return true }
                    }
                    if (visible.firstOrNull { it.isScrollable }?.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD) != true) return false
                    SystemClock.sleep(250)
                }
                return false
            }
            if (all.none { it.text?.toString()?.contains("pairing code", true) == true || it.text?.toString()?.contains("رمز الإقران", true) == true }) {
                assertTrue("Wireless debugging setting not found", click(listOf("Wireless debugging", "تصحيح الأخطاء اللاسلكي")))
                all = nodes(ins.uiAutomation.rootInActiveWindow)
            }
            val codePattern = Regex("^[0-9]{6}$")
            fun code(nodes: List<AccessibilityNodeInfo>) = nodes.asSequence().mapNotNull { it.text?.toString()?.replace(" ", "") }.firstOrNull { codePattern.matches(it) }
            if (code(all) == null) {
                assertTrue("Enable Wireless debugging and open its pairing-code dialog", click(listOf("Pair device with pairing code", "باستخدام رمز")))
                SystemClock.sleep(650)
                all = nodes(ins.uiAutomation.rootInActiveWindow)
            }
            val entered = requireNotNull(code(all)) { "Android pairing code is not visible" }
            // Never write the one-time code to logs, screenshots or test output.
            var pairingPort = 0; var connectPort = 0
            val until = SystemClock.elapsedRealtime() + 18000
            while ((pairingPort == 0 || connectPort == 0) && SystemClock.elapsedRealtime() < until) {
                ports.poll(500, TimeUnit.MILLISECONDS)?.let { if (it.first) pairingPort = it.second else connectPort = it.second }
            }
            assertTrue("Local pairing discovery failed", pairingPort > 0)
            assertTrue("Local connection discovery failed", connectPort > 0)
            if (throughService) {
                val manager = context.getSystemService(android.app.NotificationManager::class.java)
                val action = manager.activeNotifications.first { it.id == 41 }.notification.actions.first { !it.remoteInputs.isNullOrEmpty() }
                val reply = Intent()
                android.app.RemoteInput.addResultsToIntent(action.remoteInputs, reply, android.os.Bundle().apply { putCharSequence(LocalActivationService.CODE, entered) })
                action.actionIntent.send(context, 0, reply)
                val deadline = SystemClock.elapsedRealtime() + 25000
                while ((!WorkerConnection.ready || LocalActivationState.busy) && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(100)
                assertTrue("Notification activation: ${LocalActivationState.message}", WorkerConnection.ready)
                assertFalse(LocalActivationState.busy)
                SystemClock.sleep(200)
                assertFalse(manager.activeNotifications.any { it.id == 41 })
                assertFalse(requireNotNull(WorkerConnection.api).isRunning)
                return
            }
            client = LocalAdb()
            try { client.pair(pairingPort, entered) }
            catch (e: Throwable) { throw AssertionError("Pairing stage: ${client.stage}", e) }
            client.close(); SystemClock.sleep(300)
            client = LocalAdb()
            try { client.activate(context, connectPort) }
            catch (e: Throwable) { throw AssertionError("Activation stage: ${client.stage}", e) }
            assertTrue(WorkerConnection.ready)
            assertFalse(requireNotNull(WorkerConnection.api).isRunning)
        } finally {
            client?.close()
            activationPage?.let { page -> ins.runOnMainSync { page.finish() } }
            ins.runOnMainSync { discovery.close() }
        }
    }
}
