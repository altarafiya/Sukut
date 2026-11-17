package com.osama.cleanwave

import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.SystemClock
import android.view.accessibility.AccessibilityNodeInfo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** Opt-in documentation captures; never pairs, reads a PIN value or starts audio. */
@RunWith(AndroidJUnit4::class)
class GuideCaptureTest {
    @Test fun captureRealDebuggingScreens() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("captureGuides") == "true")
        val ins = InstrumentationRegistry.getInstrumentation(); val context = ins.targetContext
        val tutorialOnly=InstrumentationRegistry.getArguments().getString("tutorialOnly")=="true"
        val guideLocale=InstrumentationRegistry.getArguments().getString("guideLocale")?:"en"
        val language = UiPrefs.language(context)
        ins.runOnMainSync { PlaybackController.disable(context) }
        UiPrefs.setLanguage(context,if(tutorialOnly)guideLocale else "en")
        val page = ins.startActivitySync(Intent(context, if(tutorialOnly)SetupActivity::class.java else LocalActivationActivity::class.java)
            .putExtra("page",1).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        fun nodes(node: AccessibilityNodeInfo?): List<AccessibilityNodeInfo> = buildList {
            if (node != null) { add(node); for (i in 0 until node.childCount) addAll(nodes(node.getChild(i))) }
        }
        fun click(words: List<String>): Boolean {
            repeat(16) {
                val all = nodes(ins.uiAutomation.rootInActiveWindow)
                var button = all.firstOrNull { n -> words.any { n.text?.toString()?.equals(it,true) == true } }
                if (button != null) {
                    while (button != null && !button!!.isClickable) button = button!!.parent
                    if (button?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true) { SystemClock.sleep(550); return true }
                }
                if (all.firstOrNull { it.isScrollable }?.performAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD) != true) return false
                SystemClock.sleep(180)
            }
            return false
        }
        fun save(name: String) {
            SystemClock.sleep(220)
            val bitmap = requireNotNull(ins.uiAutomation.takeScreenshot())
            File(context.getExternalFilesDir(if(tutorialOnly)"beta16-qa" else "beta16-private"), name+".png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG,100,it) }
            bitmap.recycle()
        }
        try {
            if(tutorialOnly) {
                fun views(v:android.view.View):List<android.view.View> = buildList {
                    add(v); if(v is android.view.ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i)))
                }
                SystemClock.sleep(400); save("tutorial-top-$guideLocale")
                ins.runOnMainSync {
                    val button=views(page.window.decorView).first { it.tag=="guide_picture_wireless" }
                    button.requestRectangleOnScreen(Rect(0,0,button.width,button.height),true)
                }
                SystemClock.sleep(250); save("tutorial-buttons-$guideLocale")
                ins.runOnMainSync { views(page.window.decorView).filterIsInstance<android.widget.ScrollView>().first().fullScroll(android.view.View.FOCUS_DOWN) }
                SystemClock.sleep(300); save("tutorial-footer-$guideLocale")
                return
            }
            if(InstrumentationRegistry.getArguments().getString("aboutOnly")=="true") {
                context.startActivity(Intent(android.provider.Settings.ACTION_DEVICE_INFO_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                SystemClock.sleep(700); save("about"); return
            }
            if (InstrumentationRegistry.getArguments().getString("notificationOnly") != "true") {
            context.startActivity(Intent(android.provider.Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            SystemClock.sleep(650)
            assertTrue(click(listOf("Wireless debugging")))
            // Current Wi-Fi/network/paired-device fields are redacted before shipping.
            save("wireless")
            assertTrue(click(listOf("Pair device with pairing code")))
            save("pairing")
            // Immediately invalidate that one-time code; it is never submitted/logged.
            ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
            SystemClock.sleep(250)
            ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
            SystemClock.sleep(400)
            save("developer")
            }
            ins.runOnMainSync { page.recreate() }; SystemClock.sleep(400)
            context.startActivity(Intent(context, LocalActivationActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT))
            SystemClock.sleep(350)
            context.startForegroundService(Intent(context, LocalActivationService::class.java).setAction(LocalActivationService.PAIR))
            SystemClock.sleep(450)
            ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_NOTIFICATIONS)
            SystemClock.sleep(650)
            ins.uiAutomation.serviceInfo = ins.uiAutomation.serviceInfo.apply { flags = flags or android.accessibilityservice.AccessibilityServiceInfo.FLAG_REPORT_VIEW_IDS }
            val initial = nodes(ins.uiAutomation.rootInActiveWindow)
            val message = initial.firstOrNull { it.text?.toString()?.contains("Open Pair device",true)==true }
            val messageBounds = Rect(); message?.getBoundsInScreen(messageBounds)
            val expand = initial.firstOrNull { n ->
                val rect=Rect(); n.getBoundsInScreen(rect)
                (n.viewIdResourceName?.endsWith("/expand_button")==true || n.contentDescription?.toString()?.contains("expand",true)==true) && kotlin.math.abs(rect.centerY()-messageBounds.centerY())<220
            }
            expand?.performAction(AccessibilityNodeInfo.ACTION_CLICK)
            SystemClock.sleep(400)
            nodes(ins.uiAutomation.rootInActiveWindow).firstOrNull { it.text?.toString()?.equals("Enter pairing code",true)==true }?.performAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SHOW_ON_SCREEN.id)
            SystemClock.sleep(600)
            // Crop this capture at source to Sukut's notification only; no other apps.
            val all = nodes(ins.uiAutomation.rootInActiveWindow)
            assertTrue("Expand Sukut notification to show the pairing action", all.any { it.text?.toString()?.equals("Enter pairing code",true)==true })
            var note = all.firstOrNull { it.text?.toString()?.contains("Open Pair device",true) == true || it.text?.toString()?.contains("pairing code",true) == true }
            assertNotNull("Sukut notification was not visible",note)
            var chosen = Rect(); note!!.getBoundsInScreen(chosen)
            while (note?.parent != null) {
                val parent = note!!.parent; val bounds = Rect(); parent.getBoundsInScreen(bounds)
                if (bounds.height() > context.resources.displayMetrics.heightPixels * .50f) break
                chosen = bounds; note = parent
            }
            val screen = requireNotNull(ins.uiAutomation.takeScreenshot())
            val action = all.first { it.text?.toString()?.equals("Enter pairing code",true)==true }
            val actionBounds=Rect(); action.getBoundsInScreen(actionBounds)
            val titleBounds=Rect(); all.firstOrNull { it.text?.toString()=="Activate Sukut" }?.getBoundsInScreen(titleBounds)
            chosen=Rect(0,(if(titleBounds.isEmpty) chosen.top else titleBounds.top-35).coerceAtLeast(0),screen.width,(actionBounds.bottom+35).coerceAtMost(screen.height))
            val clipped = Bitmap.createBitmap(screen, chosen.left.coerceAtLeast(0), chosen.top.coerceAtLeast(0), chosen.width().coerceAtMost(screen.width-chosen.left), chosen.height().coerceAtMost(screen.height-chosen.top))
            File(context.getExternalFilesDir("beta16-private"),"notification.png").outputStream().use { clipped.compress(Bitmap.CompressFormat.PNG,100,it) }
            clipped.recycle(); screen.recycle()
        } finally {
            context.startService(Intent(context, LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
            if(!tutorialOnly) {
                ins.uiAutomation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
                SystemClock.sleep(250)
            }
            ins.runOnMainSync { page.finish() }; UiPrefs.setLanguage(context, language)
        }
    }
}
