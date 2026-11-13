package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.Binder
import android.os.Bundle
import android.os.SystemClock
import android.provider.Settings
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import java.io.File
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** UI-only checks: never pair, grant permissions, change debugging, or start audio. */
@RunWith(AndroidJUnit4::class)
class GuidedActivationTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext

    private fun views(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (index in 0 until view.childCount) addAll(views(view.getChildAt(index)))
    }
    private fun all(activity: Activity) = views(activity.window.decorView)
    private fun byTag(activity: Activity, tag: String) = all(activity).first { it.tag == tag }
    private fun labels(activity: Activity) = all(activity).filterIsInstance<TextView>().filter { it.isShown }.map { it.text.toString() }
    private fun click(activity: Activity, id: Int) {
        ins.runOnMainSync {
            val action = all(activity).filterIsInstance<TextView>().first { it.isShown && it.isClickable && it.text.toString() == activity.getString(id) }
            assertTrue(action.isEnabled)
            assertTrue(action.performClick())
        }
        ins.waitForIdleSync();SystemClock.sleep(250)
    }
    private fun launch(type: Class<out Activity>, page: Int? = null): Activity {
        val intent = Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        if (page != null) intent.putExtra("page", page)
        return ins.startActivitySync(intent).also { ins.waitForIdleSync(); SystemClock.sleep(350) }
    }
    private fun recreated(activity: Activity): Activity {
        val type = activity.javaClass
        ins.runOnMainSync { activity.recreate() }
        ins.waitForIdleSync(); SystemClock.sleep(400)
        lateinit var result: Activity
        ins.runOnMainSync {
            result = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { type.isInstance(it) }
        }
        return result
    }
    private fun finish(activity: Activity?) { ins.runOnMainSync { activity?.finish() }; ins.waitForIdleSync() }
    private inline fun preservingPreferences(block: () -> Unit) {
        val preferences = context.getSharedPreferences("cleanwave_ui", Context.MODE_PRIVATE)
        val originalLanguage = UiPrefs.language(context)
        val hadMethod = preferences.contains("activation_method")
        val originalMethod = UiPrefs.activationMethod(context)
        try { block() }
        finally {
            UiPrefs.setLanguage(context, originalLanguage)
            if (hadMethod) UiPrefs.setActivationMethod(context, originalMethod)
            else preferences.edit().remove("activation_method").commit()
        }
    }

    @Test fun firstRunShowsRecommendedComputerThenOnlyTheCurrentStep() = preservingPreferences {
        assumeFalse("Use an inactive test installation", LocalActivationState.busy)
        context.getSharedPreferences("cleanwave_ui", Context.MODE_PRIVATE).edit().remove("activation_method").commit()
        UiPrefs.setLanguage(context, "en")
        var page: Activity? = launch(SetupActivity::class.java, 1)
        try {
            ins.runOnMainSync {
                assertTrue(byTag(page!!, "guide_choose_computer").isSelected)
                assertFalse(byTag(page!!, "guide_choose_phone").isSelected)
                assertTrue(labels(page!!).contains(page!!.getString(R.string.guide_recommended)))
                assertFalse(all(page!!).any { it.tag == "guide_step_counter" || it.tag == "guide_pair_phone" })
                assertFalse(labels(page!!).contains(page!!.getString(R.string.guide_usb_title)))
            }
            click(page!!, R.string.next)
            ins.runOnMainSync {
                assertEquals(page!!.getString(R.string.setup_activation_progress,2,4,2,4), (byTag(page!!, "setup_step_counter") as TextView).text.toString())
                assertTrue(labels(page!!).contains(page!!.getString(R.string.guide_dev_title)))
                assertTrue(all(page!!).any {it.tag=="activation_status_0"})
                assertFalse(all(page!!).any {it.tag=="activation_status_2"})
                assertFalse(labels(page!!).contains(page!!.getString(R.string.guide_usb_title)))
            }
            click(page!!, R.string.next)
            page = recreated(page!!)
            ins.runOnMainSync {
                assertEquals(page!!.getString(R.string.setup_activation_progress,2,4,3,4), (byTag(page!!, "setup_step_counter") as TextView).text.toString())
                assertTrue(labels(page!!).contains(page!!.getString(R.string.guide_usb_title)))
                assertTrue(all(page!!).any {it.tag=="activation_status_1"})
                assertFalse(all(page!!).any {it.tag=="activation_status_0" || it.tag=="activation_status_2"})
                assertFalse(all(page!!).any { it.tag == "guide_choose_computer" })
                val footer = byTag(page!!, "setup_navigation")
                assertFalse(views(byTag(page!!, "setup_pages")).contains(footer))
            }
        } finally { finish(page) }
    }

    @Test fun phoneSelectionAndBackSurviveRecreationWithoutStartingPairing() = preservingPreferences {
        assumeTrue(android.os.Build.VERSION.SDK_INT >= 30)
        assumeFalse("Use an inactive test installation", LocalActivationState.busy)
        UiPrefs.setLanguage(context, "en"); UiPrefs.setActivationMethod(context, WorkerGuide.WINDOWS)
        var page: Activity? = launch(LocalActivationActivity::class.java)
        try {
            ins.runOnMainSync { assertTrue(byTag(page!!, "guide_choose_computer").isSelected); byTag(page!!, "guide_choose_phone").performClick() }
            click(page!!, R.string.next)
            click(page!!, R.string.next)
            page = recreated(page!!)
            ins.runOnMainSync {
                assertEquals(page!!.getString(R.string.guide_progress, 3, 4), (byTag(page!!, "guide_step_counter") as TextView).text.toString())
                // The code help sits beside the Pair button; showing it starts nothing.
                assertTrue(labels(page!!).contains(page!!.getString(R.string.guide_code_title)))
                assertTrue(all(page!!).any { it.tag == "guide_pair_phone" })
                assertFalse(LocalActivationState.busy)
            }
            click(page!!, R.string.back)
            ins.runOnMainSync { assertTrue(labels(page!!).contains(page!!.getString(R.string.guide_dev_title))) }
            click(page!!, R.string.back)
            ins.runOnMainSync {
                assertTrue(byTag(page!!, "guide_choose_phone").isSelected)
                assertEquals(page!!.getString(R.string.guide_progress,1,4),(byTag(page!!,"guide_step_counter") as TextView).text.toString())
                assertFalse(LocalActivationState.busy)
            }
        } finally { finish(page) }
    }

    @Test fun activationCannotAdvanceBeforeAnActualWorkerConnection() = preservingPreferences {
        assumeFalse("This check must not disconnect an active worker", WorkerConnection.ready)
        UiPrefs.setLanguage(context, "en")
        val page = launch(LocalActivationActivity::class.java)
        try {
            ins.runOnMainSync {
                var pairCalls = 0
                val saved = Bundle().apply { putInt("guide_method", WorkerGuide.WINDOWS); putInt("guide_step", 3) }
                val guide = WorkerGuide.Controller(page, saved, pair = { pairCalls++ }, cancelPair = {}, changed = {})
                assertFalse(guide.canNext)
                assertFalse(guide.next())
                assertEquals(3, guide.step)
                assertEquals(0, pairCalls)
                assertTrue(guide.back())
                assertEquals(1, guide.step)
                assertTrue(guide.canNext)
            }
        } finally { finish(page) }
    }

    @Test fun finalSummariesExplainStatusAndContainNoCleanupStep() = preservingPreferences {
        val page=launch(LocalActivationActivity::class.java)
        try {ins.runOnMainSync {
            for(method in listOf(WorkerGuide.PHONE)) {
                val guide=WorkerGuide.Controller(page,Bundle().apply {putInt("guide_method",method);putInt("guide_step",3)},pair={},cancelPair={},changed={})
                val all=views(guide.view)
                assertEquals(3,all.count {it.tag in listOf("activation_status_0","activation_status_1","activation_status_2","activation_status_3")})
                assertEquals(3,all.count {it.tag?.toString()?.startsWith("activation_status_detail_")==true})
                assertFalse(all.filterIsInstance<TextView>().any {it.text.toString()==page.getString(R.string.guide_turn_off_title)})
                assertTrue(guide.canNext)
            }
        }} finally {finish(page)}
    }

    @Test fun desktopLaunchResumesTheOpenGuideWithoutResettingItsStep() = preservingPreferences {
        UiPrefs.setLanguage(context,"en"); UiPrefs.setActivationMethod(context,WorkerGuide.WINDOWS)
        // First setup must still be open for the launcher to return to it.
        context.getSharedPreferences("cleanwave_ui",Context.MODE_PRIVATE).edit().putBoolean("sukut_setup_v1",false).commit()
        val page=launch(SetupActivity::class.java,1)
        try {
            click(page,R.string.next);click(page,R.string.next)
            ins.runOnMainSync { context.startActivity(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) }
            ins.waitForIdleSync();SystemClock.sleep(1200)
            ins.runOnMainSync {
                val resumed=ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                assertTrue(resumed.contains(page))
                assertEquals(page.getString(R.string.setup_activation_progress,2,4,3,4),(byTag(page,"setup_step_counter") as TextView).text.toString())
                val segments=byTag(page,"setup_progress_1") as ViewGroup
                assertEquals(4,segments.childCount)
                assertEquals(3,(0 until segments.childCount).count {segments.getChildAt(it).isSelected})
            }
        } finally {finish(page)}
    }
    @Test fun finishingSetupRemovesItsHistoryBehindHome() = preservingPreferences {
        val prefs=context.getSharedPreferences("cleanwave_ui",Context.MODE_PRIVATE)
        val had=prefs.contains("sukut_setup_v1");val previous=UiPrefs.setupCompleted(context)
        val page=launch(SetupActivity::class.java,3)
        var home:Activity?=null
        try {
            click(page,R.string.finish_setup);ins.waitForIdleSync();SystemClock.sleep(250)
            ins.runOnMainSync {
                val monitor=ActivityLifecycleMonitorRegistry.getInstance()
                home=monitor.getActivitiesInStage(Stage.RESUMED).first {it is MainActivity}
                assertTrue(home!!.isTaskRoot)
                for(stage in listOf(Stage.CREATED,Stage.STARTED,Stage.RESUMED,Stage.PAUSED,Stage.STOPPED))
                    assertFalse(monitor.getActivitiesInStage(stage).any {!it.isFinishing && (it is SetupActivity || it is LocalActivationActivity)})
            }
        } finally {
            finish(home)
            if(had)prefs.edit().putBoolean("sukut_setup_v1",previous).commit() else prefs.edit().remove("sukut_setup_v1").commit()
        }
    }

    @Test fun anAppUidCannotAcknowledgeComputerProvisioning() {
        // Calling directly tests the provider's independent shell-UID guard too,
        // rather than relying only on the manifest's DUMP permission.
        val provider = WorkerProvider()
        try {
            provider.call("computer-setup", null, null)
            fail("The app UID must not be accepted as a computer ADB shell")
        } catch (expected: SecurityException) {
            assertTrue(expected.message.orEmpty().contains("ADB shell"))
        }
    }

    @Test fun legacyCleanupOfferAndWorkerEventsNeverRedirectHome() = preservingPreferences {
        assumeFalse(WorkerConnection.ready);assumeFalse(LocalActivationState.busy)
        val prefs=context.getSharedPreferences("cleanwave_ui",Context.MODE_PRIVATE)
        val had=prefs.contains("sukut_setup_v1");val previous=UiPrefs.setupCompleted(context)
        UiPrefs.finishSetup(context)
        context.getSharedPreferences("computer_activation",Context.MODE_PRIVATE).edit().putBoolean("offer_cleanup",true).commit()
        val page=launch(MainActivity::class.java);val fake=Binder()
        try {
            ins.runOnMainSync {WorkerConnection.attach(Bundle().apply {putBinder("worker",fake);putInt("protocol",WorkerProtocol.VERSION);putInt("version",BuildConfig.VERSION_CODE);putInt("pid",12345)})}
            ins.waitForIdleSync();SystemClock.sleep(300)
            ins.runOnMainSync {
                assertTrue(ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).contains(page))
                WorkerConnection.clear(fake)
            }
            ins.waitForIdleSync();SystemClock.sleep(300)
            ins.runOnMainSync {
                assertTrue(ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).contains(page))
                val dialog=WorkerReconnect.show(page)
                assertTrue(views(dialog.window!!.decorView).any {it.tag=="reconnect_tutorial"})
                assertTrue(views(dialog.window!!.decorView).any {it.tag=="reconnect_activate"})
                dialog.dismiss()
            }
        } finally {
            ins.runOnMainSync {WorkerConnection.clear(fake)};finish(page)
            DeveloperOptionsControl.dismissCleanup(context)
            if(had)prefs.edit().putBoolean("sukut_setup_v1",previous).commit() else prefs.edit().remove("sukut_setup_v1").commit()
        }
    }
    @Test fun compactRecoveryDoesNotStartTheTutorialOrChangeDebugging() = preservingPreferences {
        assumeFalse(LocalActivationState.busy);assumeFalse(WorkerConnection.ready)
        ins.runOnMainSync {LocalActivationState.update(R.string.local_ready,false)} // A past success must not mask a lost worker.
        UiPrefs.setActivationMethod(context,WorkerGuide.WINDOWS)
        fun settings()=listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED,Settings.Global.ADB_ENABLED,"adb_wifi_enabled").map {Settings.Global.getString(context.contentResolver,it)}
        val original=settings();val page=launch(ReactivationActivity::class.java)
        try {ins.runOnMainSync {
            assertFalse(all(page).any {it.tag=="guide_windows" || it.tag=="guide_step_counter"})
            assertTrue(labels(page).contains(page.getString(R.string.show_activation_guide)))
            assertFalse(LocalActivationState.busy);assertEquals(original,settings())
            assertFalse(all(page).any {it.tag=="recovery_ready_card"});assertTrue(byTag(page,"recovery_back").width<=52*context.resources.displayMetrics.density)
        }} finally {finish(page);ins.runOnMainSync {LocalActivationState.update(0,false)}}
    }

    @Test fun liveWorkerReadyReplacesRecoveryHelpAndQuickActions() = preservingPreferences {
        assumeFalse(WorkerConnection.ready);assumeFalse(LocalActivationState.busy)
        UiPrefs.setActivationMethod(context,WorkerGuide.WINDOWS)
        val page=launch(ReactivationActivity::class.java);val binder=Binder()
        try {
            ins.runOnMainSync {WorkerConnection.attach(Bundle().apply {putBinder("worker",binder);putInt("protocol",WorkerProtocol.VERSION);putInt("version",BuildConfig.VERSION_CODE);putInt("pid",12345)})}
            ins.waitForIdleSync();SystemClock.sleep(150)
            ins.runOnMainSync {
                assertTrue(byTag(page,"recovery_ready_card").isShown)
                assertFalse(all(page).any {it.tag=="recovery_guide" || it.tag=="reactivation_start" || it.tag=="activation_status_pills"})
                assertTrue(byTag(page,"recovery_back").width<=52*context.resources.displayMetrics.density)
                WorkerConnection.clear(binder)
            }
            ins.waitForIdleSync();SystemClock.sleep(150)
            ins.runOnMainSync {assertFalse(all(page).any {it.tag=="recovery_ready_card"});assertTrue(byTag(page,"recovery_guide").isShown)}
        } finally {ins.runOnMainSync {WorkerConnection.clear(binder)};finish(page)}
    }
    @Test fun missingComputerPermissionCannotChangeAnyDebuggingSetting() {
        val denied = object : ContextWrapper(context) {
            override fun checkSelfPermission(permission: String): Int = if (permission == Manifest.permission.WRITE_SECURE_SETTINGS)
                PackageManager.PERMISSION_DENIED else super.checkSelfPermission(permission)
        }
        fun settings() = listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED, Settings.Global.ADB_ENABLED, "adb_wifi_enabled")
            .map { Settings.Global.getString(context.contentResolver, it) }
        val original = settings()
        assertFalse(DeveloperOptionsControl.setEnabled(denied, true))
        assertFalse(DeveloperOptionsControl.setEnabled(denied, false))
        assertEquals(original, settings())
    }

    @Test fun englishAndArabicKeepEnlargedTextScrollableAndNavigationVisible() = preservingPreferences {
        assumeFalse("Use an inactive test installation", LocalActivationState.busy)
        for (language in listOf("en", "ar")) {
            UiPrefs.setLanguage(context, language); UiPrefs.setActivationMethod(context, WorkerGuide.WINDOWS)
            val page = launch(SetupActivity::class.java, 1)
            try {
                ins.runOnMainSync {
                    // Exercise wrapping with enlarged labels without changing the
                    // device-wide font setting or the user's Android preferences.
                    all(page).filterIsInstance<TextView>().forEach { it.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, it.textSize * 1.6f) }
                    page.window.decorView.requestLayout()
                }
                ins.waitForIdleSync(); SystemClock.sleep(250)
                ins.runOnMainSync {
                    val footer = byTag(page, "setup_navigation")
                    val visible = Rect()
                    assertTrue(footer.getGlobalVisibleRect(visible))
                    assertTrue(visible.height() > 0)
                    val next = views(footer).filterIsInstance<TextView>().first { it.text.toString() == page.getString(R.string.next) }
                    assertTrue(next.getGlobalVisibleRect(Rect()))
                    assertTrue(next.isEnabled)
                    val text = all(page).filterIsInstance<TextView>().first { it.text.toString() == page.getString(R.string.guide_computer_benefit) }
                    assertEquals(if (language == "ar") View.TEXT_DIRECTION_RTL else View.TEXT_DIRECTION_LTR, text.textDirection)
                    assertNotNull(text.layout)
                    assertTrue(text.height >= text.layout.height + text.compoundPaddingTop + text.compoundPaddingBottom)
                    assertFalse(views(byTag(page, "setup_pages")).contains(footer))
                }
            } finally { finish(page) }
        }
    }

    @Test fun capturePolishedPhoneStepsWhenRequested() = preservingPreferences {
        assumeTrue(InstrumentationRegistry.getArguments().getString("captureGuidedActivation")=="true")
        assumeFalse(WorkerConnection.ready)
        for(language in listOf("en","ar")) {
            UiPrefs.setLanguage(context,language);UiPrefs.setActivationMethod(context,WorkerGuide.PHONE)
            val page=launch(LocalActivationActivity::class.java);val binder=Binder()
            fun capture(name:String) {
                ins.waitForIdleSync();SystemClock.sleep(300)
                val bitmap=requireNotNull(ins.uiAutomation.takeScreenshot())
                File(context.getExternalFilesDir("guided-activation-qa"),"$language-$name.png").outputStream().use {bitmap.compress(Bitmap.CompressFormat.PNG,100,it)};bitmap.recycle()
            }
            try {
                click(page,R.string.next);click(page,R.string.next)
                ins.runOnMainSync {
                    val photo=byTag(page,"guide_picture_notification")
                    assertTrue((photo.layoutParams as ViewGroup.MarginLayoutParams).bottomMargin>=12*context.resources.displayMetrics.density)
                }
                capture("pair")
                // Next waits for a real worker on the Pair page.
                ins.runOnMainSync {WorkerConnection.attach(Bundle().apply {putBinder("worker",binder);putInt("protocol",WorkerProtocol.VERSION);putInt("version",BuildConfig.VERSION_CODE);putInt("pid",12345)})}
                ins.waitForIdleSync();SystemClock.sleep(300);click(page,R.string.next);capture("phone-ready")
            } finally {ins.runOnMainSync {WorkerConnection.clear(binder)};finish(page)}
        }
    }

    @Test fun captureGuidedSetupScreensWhenExplicitlyRequested() = preservingPreferences {
        assumeTrue(InstrumentationRegistry.getArguments().getString("captureGuidedActivation") == "true")
        assumeFalse(LocalActivationState.busy)
        for (language in listOf("en", "ar")) {
            UiPrefs.setLanguage(context, language); UiPrefs.setActivationMethod(context, WorkerGuide.WINDOWS)
            val page = launch(SetupActivity::class.java, 1)
            fun capture(name: String) {
                ins.waitForIdleSync(); SystemClock.sleep(250)
                val screenshot = requireNotNull(ins.uiAutomation.takeScreenshot())
                File(context.getExternalFilesDir("guided-activation-qa"), "$language-$name.png").outputStream().use {
                    screenshot.compress(Bitmap.CompressFormat.PNG, 100, it)
                }
                screenshot.recycle()
            }
            try {
                capture("choose")
                click(page, R.string.next); capture("developer")
                click(page, R.string.next); capture("usb")
                click(page, R.string.next); capture("activate")
            } finally { finish(page) }
        }
    }
}
