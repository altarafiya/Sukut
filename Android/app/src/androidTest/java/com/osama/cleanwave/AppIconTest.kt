package com.osama.cleanwave

import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Choosing an icon keeps Settings open; the launcher alias switches after leaving Sukut. */
@RunWith(AndroidJUnit4::class)
class AppIconTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private fun views(v: View): List<View> = buildList { add(v); if (v is ViewGroup) for (i in 0 until v.childCount) addAll(views(v.getChildAt(i))) }
    private fun state(mark: String, color: String) = context.packageManager.getComponentEnabledSetting(ComponentName(context.packageName, "com.osama.cleanwave.Launcher_${mark}_$color"))

    @Test fun choosingAnIconDoesNotRestartAndAppliesAfterLeaving() {
        val originalMark = AppIcon.mark(context); val originalColor = AppIcon.color(context)
        // Start from the default icon, so the click below is a real change.
        AppIcon.choose(context, AppIcon.DEFAULT_MARK, AppIcon.DEFAULT_COLOR); AppIcon.applyPending(context)
        try {
            val page = ins.startActivitySync(Intent(context, SettingsActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            ins.waitForIdleSync()
            ins.runOnMainSync {
                val marks = views(page.window.decorView).first { it.tag == "app_icon_marks" } as ViewGroup
                marks.getChildAt(0).performClick() // "soft"
            }
            ins.waitForIdleSync(); SystemClock.sleep(300)
            ins.runOnMainSync {
                assertTrue("Settings must stay the same screen", ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).contains(page))
                assertFalse(page.isFinishing)
                assertTrue((views(page.window.decorView).first { it.tag == "app_icon_marks" } as ViewGroup).getChildAt(0).isSelected)
            }
            assertEquals("soft", AppIcon.mark(context))
            assertNotEquals("The launcher must not change while Sukut is open", PackageManager.COMPONENT_ENABLED_STATE_ENABLED, state("soft", AppIcon.color(context)))
            ins.runOnMainSync { page.finish() }
            // The switch happens once the last Sukut screen has stopped.
            val deadline = SystemClock.uptimeMillis() + 8000
            while (state("soft", AppIcon.color(context)) != PackageManager.COMPONENT_ENABLED_STATE_ENABLED && SystemClock.uptimeMillis() < deadline) SystemClock.sleep(200)
            assertEquals(PackageManager.COMPONENT_ENABLED_STATE_ENABLED, state("soft", AppIcon.color(context)))
            assertEquals(PackageManager.COMPONENT_ENABLED_STATE_DISABLED, state("blue", "black"))
        } finally {
            AppIcon.choose(context, originalMark, originalColor); AppIcon.applyPending(context)
        }
    }
    @Test fun captureIconSettingsWhenRequested() {
        org.junit.Assume.assumeTrue(InstrumentationRegistry.getArguments().getString("captureSettings") == "true")
        val language = UiPrefs.language(context)
        try { for (locale in listOf("en", "ar")) {
            UiPrefs.setLanguage(context, locale)
            val page = ins.startActivitySync(Intent(context, SettingsActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            ins.waitForIdleSync()
            ins.runOnMainSync { views(page.window.decorView).first { it.tag == "app_icon_marks" }.let { it.parent.requestChildFocus(it, it); (it as View).requestRectangleOnScreen(android.graphics.Rect(0, 0, it.width, it.height * 6), true) } }
            ins.waitForIdleSync(); SystemClock.sleep(500)
            val shot = requireNotNull(ins.uiAutomation.takeScreenshot())
            java.io.File(context.getExternalFilesDir("icon-settings-qa"), "$locale.png").outputStream().use { shot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }
            ins.runOnMainSync { page.finish() }; ins.waitForIdleSync()
        } } finally { UiPrefs.setLanguage(context, language) }
    }
}
