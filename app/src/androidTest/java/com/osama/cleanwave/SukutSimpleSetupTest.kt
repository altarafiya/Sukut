package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class SukutSimpleSetupTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private fun views(v: View): List<View> = buildList {
        add(v); if (v is ViewGroup) for (i in 0 until v.childCount) addAll(views(v.getChildAt(i)))
    }
    private fun shot(name: String) {
        ins.waitForIdleSync(); SystemClock.sleep(400)
        val bitmap = ins.uiAutomation.takeScreenshot() ?: error("No screenshot")
        val dir = File(context.filesDir, "simple-0.3.23").apply { mkdirs() }
        File(dir, "$name.png").outputStream().use { bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }
    private fun clickLabel(activity: Activity, id: Int) {
        ins.runOnMainSync {
            val label = views(activity.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == activity.getString(id) }
            var action: View = label
            while (!action.isClickable) action = action.parent as View
            assertTrue(action.performClick())
        }
        ins.waitForIdleSync()
    }
    private fun reveal(activity: Activity, tag: String) {
        ins.runOnMainSync {
            val all = views(activity.window.decorView)
            val scroll = all.filterIsInstance<android.widget.ScrollView>().first()
            val target = all.first { it.tag == tag }
            val bounds = android.graphics.Rect()
            target.getDrawingRect(bounds)
            scroll.offsetDescendantRectToMyCoords(target, bounds)
            scroll.scrollTo(0, maxOf(0, bounds.top - MobileUi(activity).dp(8)))
        }
        ins.waitForIdleSync(); SystemClock.sleep(200)
    }
    private fun setup(page: Int, landscape: Boolean): Activity {
        val activity = ins.startActivitySync(Intent(context, SetupActivity::class.java).putExtra("page", page).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        ins.runOnMainSync { activity.requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
        ins.waitForIdleSync(); SystemClock.sleep(650)
        var resumed: Activity? = null
        ins.runOnMainSync { resumed = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is SetupActivity } }
        return resumed!!
    }
    @Test fun homeSettingsAndBothSetupMethodsInEnglishAndArabic() {
        val before = UiPrefs.language(context)
        try {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand("am start -W -n ${context.packageName}/com.osama.cleanwave.MainActivity")).bufferedReader().use { it.readText() }
            ins.waitForIdleSync(); SystemClock.sleep(600)
            for (language in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, language)
                var home: MainActivity? = null
                ins.runOnMainSync { home = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).filterIsInstance<MainActivity>().firstOrNull(); home?.recreate() }
                ins.waitForIdleSync(); SystemClock.sleep(600)
                ins.runOnMainSync {
                    home = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).filterIsInstance<MainActivity>().first()
                    val all = views(home!!.window.decorView)
                    val modes = all.first { it.tag == "audio_modes_card" } as ViewGroup
                    assertTrue("Tip must be inside Audio mode", views(modes).any { it.tag == "mode_tip" })
                    val quality = all.first { it.tag == "home_sound_quality" }
                    assertTrue(quality.isClickable)
                    assertTrue("Compact Home row", quality.height <= MobileUi(home!!).dp(110))
                    assertFalse(views(quality).filterIsInstance<TextView>().any { it.text.toString() == home!!.getString(R.string.performance_help) })
                    assertFalse(views(quality).filterIsInstance<TextView>().any { it.text.toString() == home!!.getString(R.string.change_sound_quality) })
                }
                clickLabel(home!!, R.string.engine_performance)
                ins.runOnMainSync {
                    val dialog = MainActivity::class.java.getDeclaredField("performanceDialog").apply { isAccessible = true }.get(home) as android.app.Dialog
                    assertTrue(views(dialog.window!!.decorView).any { it.tag == "engine_performance_choices" })
                    dialog.dismiss()
                }
                shot("$language-home")
                ins.runOnMainSync { home!!.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE }
                ins.waitForIdleSync(); SystemClock.sleep(650)
                ins.runOnMainSync { home = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).filterIsInstance<MainActivity>().first() }
                shot("$language-home-landscape")
                ins.runOnMainSync { home!!.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
                ins.waitForIdleSync(); SystemClock.sleep(650)
                val settings = ins.startActivitySync(Intent(context, SettingsActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                ins.waitForIdleSync()
                ins.runOnMainSync {
                    val labels = views(settings.window.decorView).filterIsInstance<TextView>().map { it.text.toString() }
                    assertFalse(labels.contains(settings.getString(R.string.engine_performance)))
                    assertFalse(labels.contains(settings.getString(R.string.source_app)))
                }
                shot("$language-settings"); ins.runOnMainSync { settings.finish() }; ins.waitForIdleSync()
                for (landscape in listOf(false, true)) {
                    val welcome = setup(0, landscape)
                    if (language == "ar") ins.runOnMainSync {
                        val caption = views(welcome.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == welcome.getString(R.string.setup_welcome_body) }
                        assertEquals("Arabic paragraph on an English-system phone", -1, caption.layout.getParagraphDirection(0))
                        assertEquals("ar", caption.textLocale.language)
                    }
                    shot("$language-${if (landscape) "landscape" else "portrait"}-welcome"); ins.runOnMainSync { welcome.finish() }; ins.waitForIdleSync()
                    val guide = setup(1, landscape)
                    ins.runOnMainSync {
                        for (index in 0..1) {
                            val button = views(guide.window.decorView).first { it.tag == "shizuku_method_$index" }
                            assertTrue(button.isClickable); assertTrue(button.isFocusable)
                            assertTrue(button.height >= MobileUi(guide).dp(60))
                            assertEquals("android.widget.Button", button.createAccessibilityNodeInfo().className.toString())
                            val panel = views(guide.window.decorView).first { it.tag == "shizuku_method_content_$index" }
                            assertEquals(View.GONE, panel.visibility)
                        }
                    }
                    reveal(guide, "shizuku_method_0")
                    shot("$language-${if (landscape) "landscape" else "portrait"}-method-buttons")
                    clickLabel(guide, R.string.guide_pc_title); reveal(guide, "shizuku_method_content_0"); shot("$language-${if (landscape) "landscape" else "portrait"}-usb-guide")
                    clickLabel(guide, R.string.guide_wireless_title)
                    // AutoTransition temporarily keeps the outgoing panel visible.
                    // Assert the settled state, not a frame during the animation.
                    SystemClock.sleep(350); ins.waitForIdleSync()
                    ins.runOnMainSync {
                        assertEquals(View.GONE, views(guide.window.decorView).first { it.tag == "shizuku_method_content_0" }.visibility)
                        assertEquals(View.VISIBLE, views(guide.window.decorView).first { it.tag == "shizuku_method_content_1" }.visibility)
                        val all = views(guide.window.decorView)
                        val step0 = all.first { it.tag == "shizuku_step_1_0" }
                        val step2 = all.first { it.tag == "shizuku_step_1_2" }
                        val step3 = all.first { it.tag == "shizuku_step_1_3" }
                        assertTrue(views(step0).filterIsInstance<TextView>().any { it.text.toString() == guide.getString(R.string.guide_google_play) })
                        for (label in listOf(R.string.guide_developer_options, R.string.guide_wireless_picture))
                            assertTrue("Debugging action/picture belong to the debugging step", views(step2).filterIsInstance<TextView>().any { it.text.toString() == guide.getString(label) })
                        assertTrue("Pairing picture belongs to pairing step", views(step3).filterIsInstance<TextView>().any { it.text.toString() == guide.getString(R.string.guide_shizuku_picture) })
                    }; reveal(guide, "shizuku_method_content_1"); shot("$language-${if (landscape) "landscape" else "portrait"}-wireless-guide")
                    reveal(guide, "shizuku_step_1_2"); shot("$language-${if (landscape) "landscape" else "portrait"}-debugging-actions")
                    reveal(guide, "shizuku_step_1_3"); shot("$language-${if (landscape) "landscape" else "portrait"}-pairing-actions")
                    ins.runOnMainSync { guide.finish() }; ins.waitForIdleSync()
                    val sound = setup(2, landscape)
                    ins.runOnMainSync { assertTrue(views(sound.window.decorView).any { it.tag == "engine_performance_choices" }) }
                    shot("$language-${if (landscape) "landscape" else "portrait"}-sound")
                    ins.runOnMainSync { sound.finish() }; ins.waitForIdleSync()
                }
                ins.runOnMainSync { home?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED }
                ins.waitForIdleSync()
            }
        } finally { UiPrefs.setLanguage(context, before) }
    }
}
