package com.osama.cleanwave

import android.app.NotificationManager
import android.content.Intent
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Driver temporarily resets only POST_NOTIFICATIONS; no code is read or submitted. */
@RunWith(AndroidJUnit4::class)
class FirstPairingNotificationTest {
    @Test fun firstGrantPostsTheReplyBeforeOpeningWirelessDebugging() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("firstNotification")=="true")
        val ins=InstrumentationRegistry.getInstrumentation(); val context=ins.targetContext
        val language=UiPrefs.language(context); UiPrefs.setLanguage(context,"en")
        val page=ins.startActivitySync(Intent(context,LocalActivationActivity::class.java).putExtra("method",0)
            .putExtra("activation_action",LocalActivationService.PAIR).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        fun nodes(v:AccessibilityNodeInfo?):List<AccessibilityNodeInfo> = buildList { if(v!=null) { add(v); for(i in 0 until v.childCount)addAll(nodes(v.getChild(i))) } }
        fun waitFor(ms:Long,condition:()->Boolean) {
            val until=SystemClock.elapsedRealtime()+ms
            while(!condition() && SystemClock.elapsedRealtime()<until)SystemClock.sleep(70)
            assertTrue("First notification/Settings condition timed out",condition())
        }
        try {
            waitFor(4000) { nodes(ins.uiAutomation.rootInActiveWindow).any { it.text?.toString()=="Allow" && it.isClickable } }
            assertTrue(nodes(ins.uiAutomation.rootInActiveWindow).first { it.text?.toString()=="Allow" && it.isClickable }.performAction(AccessibilityNodeInfo.ACTION_CLICK))
            waitFor(5000) { ins.uiAutomation.rootInActiveWindow?.packageName?.toString()=="com.android.settings" }
            assertTrue(LocalActivationState.notificationReady)
            val note=context.getSystemService(NotificationManager::class.java).activeNotifications.first { it.id==41 }.notification
            assertTrue(note.actions.any { it.remoteInputs?.isNotEmpty()==true })
            ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_NOTIFICATIONS)
            waitFor(2000) { nodes(ins.uiAutomation.rootInActiveWindow).any { it.text?.toString()?.contains(page.getString(R.string.local_title))==true } }
            assertFalse("Pairing alone must leave audio Off",CleanerPrefs.isEnabled(context))
        } finally {
            ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
            SystemClock.sleep(400); ins.waitForIdleSync()
            context.startService(Intent(context,LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
            SystemClock.sleep(200); ins.runOnMainSync { page.finish() }; UiPrefs.setLanguage(context,language)
        }
    }
}
