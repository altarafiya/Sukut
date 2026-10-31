package com.osama.cleanwave

import android.content.Intent
import android.net.Uri
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in existing-pair checks. No PIN reading, other-app audio or volume changes. */
@RunWith(AndroidJUnit4::class)
class PairedActivationPhoneTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private fun views(v: View): List<View> = buildList {
        add(v); if(v is ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i)))
    }
    private fun waitFor(timeout: Long = 25000, condition: () -> Boolean) {
        val deadline=SystemClock.elapsedRealtime()+timeout
        while(!condition() && SystemClock.elapsedRealtime()<deadline)SystemClock.sleep(80)
        assertTrue("Activation condition timed out; state=${LocalActivationState.message}",condition())
    }
    @Test fun mainPowerActivatesSavedPairingReturnsAndStartsOnlyOwnUid() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("localActivate")=="true")
        val source=CleanerPrefs.sourceApp(context); val language=UiPrefs.language(context)
        val method=UiPrefs.activationMethod(context); UiPrefs.setActivationMethod(context,0)
        ins.runOnMainSync { PlaybackController.disable(context) }
        waitFor(5000) { !CleanerService.isAlive && !CleanerPrefs.startPending(context) }
        UiPrefs.setLanguage(context,"en"); CleanerPrefs.setSourceApp(context,context.packageName)
        val home=ins.startActivitySync(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        val monitor=ins.addMonitor(LocalActivationActivity::class.java.name,null,false)
        try {
            context.contentResolver.call(Uri.parse("content://com.altarafiyah.sukut.worker"),"deactivate",null,null)
            waitFor(5000) { !WorkerConnection.ready }
            ins.runOnMainSync { views(home.window.decorView).first { it.tag=="power_button" }.performClick() }
            assertNotNull("Power must open activation when inactive",ins.waitForMonitorWithTimeout(monitor,4000))
            waitFor { WorkerConnection.ready && !LocalActivationState.busy && CleanerPrefs.isEnabled(context) && WorkerConnection.api?.isRunning==true }
            assertTrue("Return to Home after activation",ins.uiAutomation.rootInActiveWindow?.packageName?.toString()==context.packageName)
            assertTrue("Home content visible",ins.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(home.getString(R.string.audio_mode))?.isNotEmpty()==true)
            assertFalse(context.getSystemService(android.app.NotificationManager::class.java).activeNotifications.any { it.id==41 })
        } finally {
            ins.removeMonitor(monitor)
            ins.runOnMainSync { PlaybackController.disable(context); home.finish() }
            waitFor(5000) { !CleanerService.isAlive && WorkerConnection.api?.isRunning!=true }
            CleanerPrefs.setSourceApp(context,source); UiPrefs.setLanguage(context,language)
            UiPrefs.setActivationMethod(context,method)
            PowerActivationRequest.cancel()
        }
    }
    @Test fun pairOpensWirelessDirectlyAndOwnedSettingsClosesOnReady() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("localActivate")=="true")
        assertTrue("Run with an activated worker",WorkerConnection.ready)
        val language=UiPrefs.language(context); UiPrefs.setLanguage(context,"en")
        val page=ins.startActivitySync(Intent(context,LocalActivationActivity::class.java)
            .putExtra("activation_action",LocalActivationService.PAIR)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        fun nodes(v: AccessibilityNodeInfo?):List<AccessibilityNodeInfo> = buildList {
            if(v!=null) { add(v); for(i in 0 until v.childCount)addAll(nodes(v.getChild(i))) }
        }
        try {
            waitFor(5000) { nodes(ins.uiAutomation.rootInActiveWindow).any { it.text?.toString()=="Pair device with pairing code" } }
            assertEquals("com.android.settings",ins.uiAutomation.rootInActiveWindow?.packageName?.toString())
            // Uses an already-ready endpoint to exercise the real service/state return
            // while Settings is foreground, without opening or reading another PIN.
            context.startService(Intent(context,LocalActivationService::class.java).setAction(LocalActivationService.ACTIVATE))
            waitFor(6000) { !LocalActivationState.busy && ins.uiAutomation.rootInActiveWindow?.packageName?.toString()==context.packageName }
            assertFalse("Pairing from Settings must leave audio Off",CleanerPrefs.isEnabled(context))
            assertFalse(requireNotNull(WorkerConnection.api).isRunning)
        } finally {
            if(LocalActivationState.busy)context.startService(Intent(context,LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
            ins.runOnMainSync { page.finish() }; UiPrefs.setLanguage(context,language)
        }
    }
}
