package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Rect
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ScrollView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class SukutTabletPerformanceTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private val output get() = File(context.filesDir, "tablet-0.3.17").apply { mkdirs() }
    private val report get() = File(output, "checks.txt")
    private fun views(v: View): List<View> = buildList {
        add(v); if (v is ViewGroup) for (i in 0 until v.childCount) addAll(views(v.getChildAt(i)))
    }
    private fun <T : Activity> launch(type: Class<T>, page: Int = 0): T {
        // Xiaomi blocks an instrumented app's first background Activity launch.
        // Start its ordinary exported launcher as shell, then use in-app launches.
        val command = "am start -W -n ${context.packageName}/com.osama.cleanwave.MainActivity"
        android.os.ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand(command)).bufferedReader().use { it.readText() }
        ins.waitForIdleSync(); SystemClock.sleep(600)
        var activity: Activity? = null
        if (type == MainActivity::class.java) ins.runOnMainSync {
            activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is MainActivity }
        } else activity = ins.startActivitySync(Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK).putExtra("page", page))
        ins.waitForIdleSync(); SystemClock.sleep(450)
        @Suppress("UNCHECKED_CAST") return activity as T
    }
    private fun click(activity: Activity, text: Int) = ins.runOnMainSync {
        val label = views(activity.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == activity.getString(text) }
        var action: View = label
        while (!action.isClickable) action = action.parent as View
        assertTrue(action.isEnabled); assertTrue(action.performClick())
    }
    private fun tagged(activity: Activity, tag: String) = views(activity.window.decorView).first { it.tag == tag }
    private fun waitFor(label: String, condition: () -> Boolean) {
        val end = SystemClock.elapsedRealtime() + 20000
        while (SystemClock.elapsedRealtime() < end) { if (condition()) return; SystemClock.sleep(100) }
        fail("Timed out: $label; ${CleanerPrefs.status(context)} ${CleanerPrefs.error(context)}")
    }
    private fun screenshot(name: String) {
        ins.waitForIdleSync(); SystemClock.sleep(300)
        val shot = ins.uiAutomation.takeScreenshot() ?: error("Screenshot unavailable")
        File(output, "$name.png").outputStream().use { shot.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it) }; shot.recycle()
    }
    private fun restore(prefs: SharedPreferences, saved: Map<String, *>) {
        prefs.edit().clear().apply {
            saved.forEach { (key, value) -> when (value) {
                is String -> putString(key, value); is Boolean -> putBoolean(key, value)
                is Int -> putInt(key, value); is Long -> putLong(key, value); is Float -> putFloat(key, value)
                is Set<*> -> @Suppress("UNCHECKED_CAST") putStringSet(key, value as Set<String>)
            } }
        }.commit()
    }
    private fun footer(activity: Activity, id: Int) = ins.runOnMainSync {
        val next = views(activity.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == activity.getString(id) }
        val bounds = Rect(); assertTrue(next.getGlobalVisibleRect(bounds))
        val root = activity.window.decorView
        val bottom = root.rootWindowInsets?.getInsets(android.view.WindowInsets.Type.navigationBars())?.bottom ?: 0
        assertTrue("Footer touches navigation bar", bounds.bottom < root.height - bottom - 12)
    }

    private fun setOption(root: View, tag: String, position: Int) = ins.runOnMainSync {
        when (val control = views(root).first { it.tag == tag }) {
            is android.widget.Switch -> if (control.isChecked != (position == 1)) control.performClick()
            is SegmentedChoice -> control.getChildAt(position).performClick()
            else -> error("Unknown playback control")
        }
    }

    @Test fun playbackControlsInSettingsAndSetup() {
        val prefs = context.getSharedPreferences("cleanwave_state", 0)
        val uiPrefs = context.getSharedPreferences("cleanwave_ui", 0)
        val saved = prefs.all.toMap(); val savedUi = uiPrefs.all.toMap()
        assertFalse("Do not interrupt an existing session", CleanerPrefs.isEnabled(context))
        try {
            UiPrefs.finishSetup(context)
            for ((language, theme) in listOf("en" to UiPrefs.THEME_LIGHT, "ar" to UiPrefs.THEME_DARK)) {
                UiPrefs.setLanguage(context, language); UiPrefs.setTheme(context, theme)
                val home = launch(MainActivity::class.java)
                try {
                    click(home, R.string.engine_performance); SystemClock.sleep(250)
                    val dialog = MainActivity::class.java.getDeclaredField("performanceDialog").apply { isAccessible = true }.get(home) as Dialog
                    setOption(dialog.window!!.decorView, "phone_effects_switch", 1)
                    for (index in 0..2) { setOption(dialog.window!!.decorView, "audio_buffering", index); assertEquals(index, CleanerPrefs.buffering(context)); assertTrue(CleanerPrefs.effectsCompatibility(context)) }
                    setOption(dialog.window!!.decorView, "phone_effects_switch", 0)
                    assertEquals(2, CleanerPrefs.buffering(context)); assertFalse(CleanerPrefs.effectsCompatibility(context))
                    assertFalse(CleanerPrefs.isEnabled(context)); assertFalse(CleanerPrefs.startPending(context))
                    screenshot("playback-$language")
                    ins.runOnMainSync { dialog.dismiss() }
                } finally { ins.runOnMainSync { home.finish() } }
                val setup = launch(SetupActivity::class.java, 2)
                try {
                    assertTrue(tagged(setup, "phone_effects_switch") is android.widget.Switch)
                    assertEquals(3, (tagged(setup, "audio_buffering") as SegmentedChoice).childCount)
                    screenshot("setup-playback-$language")
                } finally { ins.runOnMainSync { setup.finish() } }
            }
        } finally { restore(prefs, saved); restore(uiPrefs, savedUi) }
    }

    @Test fun landscapeSetupSettingsAndLivePerformance() {
        val prefs = context.getSharedPreferences("cleanwave_state", 0)
        val uiPrefs = context.getSharedPreferences("cleanwave_ui", 0)
        val saved = prefs.all.toMap(); val savedUi = uiPrefs.all.toMap()
        assertFalse("Do not interrupt an existing user session", CleanerPrefs.isEnabled(context))
        report.writeText("version=${BuildConfig.VERSION_NAME}; orientation=${context.resources.configuration.orientation}\n")
        try {
            // Fresh defaults without clearing app data, files, grants or models.
            prefs.edit().remove("buffering").remove("effects_compatibility").remove("engine_performance_custom").commit()
            assertEquals(1, CleanerPrefs.buffering(context)); assertFalse(CleanerPrefs.effectsCompatibility(context))
            assertEquals(EnginePerformanceConfig.BALANCED, EnginePerformance.selected(context))
            UiPrefs.finishSetup(context)
            for (language in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, language)
                val setup = launch(SetupActivity::class.java)
                try {
                    assertEquals(android.content.res.Configuration.ORIENTATION_LANDSCAPE, setup.resources.configuration.orientation)
                    footer(setup, R.string.next); screenshot("setup-$language-1")
                    click(setup, R.string.next); SystemClock.sleep(400); footer(setup, R.string.next); screenshot("setup-$language-2")
                    click(setup, R.string.next); SystemClock.sleep(400); footer(setup, R.string.next); screenshot("setup-$language-3")
                    setOption(setup.window.decorView, "phone_effects_switch", 1)
                    SystemClock.sleep(300); assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformance.selected(context))
                    ins.runOnMainSync { views(setup.window.decorView).filterIsInstance<ScrollView>().first().fullScroll(View.FOCUS_DOWN) }
                    SystemClock.sleep(250)
                    footer(setup, R.string.next); screenshot("setup-$language-custom")
                    click(setup, R.string.next); SystemClock.sleep(400); footer(setup, R.string.finish_setup); screenshot("setup-$language-4")
                } finally { ins.runOnMainSync { setup.finish() } }
                val settings = launch(MainActivity::class.java)
                try {
                    ins.runOnMainSync {
                        val text = views(settings.window.decorView).filterIsInstance<TextView>().map { it.text.toString() }
                        assertEquals(1, text.count { it == settings.getString(R.string.engine_performance) })
                        assertFalse(text.contains(settings.getString(R.string.output_quality)))
                        assertFalse(text.contains(settings.getString(R.string.buffering)))
                    }
                    screenshot("settings-$language")
                    click(settings, R.string.engine_performance); SystemClock.sleep(300)
                    val dialog = MainActivity::class.java.getDeclaredField("performanceDialog").apply { isAccessible = true }.get(settings) as Dialog
                    setOption(dialog.window!!.decorView, "phone_effects_switch", 1)
                    waitFor("custom higher quality saved") { CleanerPrefs.effectsCompatibility(context) }
                    setOption(dialog.window!!.decorView, "audio_buffering", 2)
                    waitFor("custom maximum saved") { CleanerPrefs.buffering(context) == 2 }
                    ins.runOnMainSync { views(dialog.window!!.decorView).filterIsInstance<ScrollView>().first().fullScroll(View.FOCUS_DOWN) }
                    screenshot("performance-$language-custom")
                    ins.runOnMainSync { dialog.dismiss() }
                } finally { ins.runOnMainSync { settings.finish() } }
            }
            report.appendText("PASS landscape setup4pages/fixedfooter/custom and simplifiedSettings EN/AR\n")
            UiPrefs.setLanguage(context, "en")
            ins.runOnMainSync { EnginePerformance.select(context, EnginePerformanceConfig.BALANCED); CleanerPrefs.setSourcePreset(context, CleanerPrefs.SOURCE_OPTIMIZED) }
            val home = launch(MainActivity::class.java)
            try {
                ins.runOnMainSync { tagged(home, "mode_0").performClick(); assertFalse(CleanerPrefs.isEnabled(context)); tagged(home, "power_button").performClick() }
                waitFor("active Optimized") { CleanerPrefs.status(context) == "Active" && CleanerService.routeGeneration > 0 }
                assertEquals(-100, CleanerPrefs.sourceMusicDb(context)); assertEquals(0, CleanerPrefs.sourceVoiceDb(context))
                val generation = CleanerService.routeGeneration
                val slider = tagged(home, "optimized_background") as SeekBar
                val initial = CleanerPrefs.optimizedBackground(context)
                val bounds = Rect(); ins.runOnMainSync { assertTrue(slider.getGlobalVisibleRect(bounds)); assertEquals(30, slider.max) }
                val down = SystemClock.uptimeMillis(); val y = bounds.centerY().toFloat()
                val start = bounds.left + slider.paddingLeft + (bounds.width() - slider.paddingLeft - slider.paddingRight) * ((initial + 20) / 30f)
                fun touch(action: Int, x: Float) { ins.sendPointerSync(MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, y, 0)); ins.waitForIdleSync() }
                touch(MotionEvent.ACTION_DOWN, start)
                touch(MotionEvent.ACTION_MOVE, bounds.right - slider.paddingRight - 12f)
                assertEquals("Audio changed during drag", initial, CleanerPrefs.optimizedBackground(context))
                touch(MotionEvent.ACTION_UP, bounds.right - slider.paddingRight - 12f)
                waitFor("background release commit") { CleanerPrefs.optimizedBackground(context) != initial }
                assertTrue(CleanerPrefs.optimizedBackground(context) in -20..10)
                SystemClock.sleep(600); assertEquals("Gain restarted model", generation, CleanerService.routeGeneration)
                screenshot("home-en-optimized")
                for (mode in listOf(0, 1, 2)) {
                    val before = CleanerService.routeGeneration
                    ins.runOnMainSync { EnginePerformance.select(context, mode) }
                    waitFor("preset $mode active") { CleanerPrefs.status(context) == "Active" && (mode == 1 && before == generation || CleanerService.routeGeneration > before) }
                    val pair = EnginePerformanceConfig.presets[mode]
                    assertEquals(pair.effects, CleanerPrefs.effectsCompatibility(context)); assertEquals(pair.buffering, CleanerPrefs.buffering(context))
                    val stable = CleanerService.routeGeneration
                    ins.runOnMainSync { EnginePerformance.select(context, mode) }; SystemClock.sleep(500)
                    assertEquals("Same preset restarted", stable, CleanerService.routeGeneration)
                }
                val beforeLowest = CleanerService.routeGeneration
                ins.runOnMainSync { EnginePerformance.select(context, 0) }
                waitFor("lowest active") { CleanerService.routeGeneration > beforeLowest && CleanerPrefs.status(context) == "Active" && CleanerPrefs.buffering(context) == 0 }
                for (next in listOf(1, 2)) {
                    ins.runOnMainSync { CleanerPrefs.recordRecovery(context, true) }; SystemClock.sleep(300)
                    val before = CleanerService.routeGeneration
                    ins.runOnMainSync {
                        val warning = views(home.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == home.getString(R.string.buffer_delay_warning) }
                        assertEquals(MobileUi(home).warning, warning.currentTextColor)
                    }
                    screenshot("recovery-next-$next")
                    ins.runOnMainSync { tagged(home, "increase_buffering").performClick() }
                    waitFor("next buffering active") { CleanerService.routeGeneration > before && CleanerPrefs.status(context) == "Active" }
                    assertEquals(next, CleanerPrefs.buffering(context)); assertFalse(CleanerPrefs.effectsCompatibility(context))
                }
                assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformance.selected(context))
                ins.runOnMainSync { PlaybackController.disable(context) }; waitFor("OFF") { !CleanerService.isAlive }
                screenshot("home-en-off")
                report.appendText("PASS Optimized musicmute/voice0/backgroundrelease;3presets atomic active restart/sameselection no-op; oneclick0→1→2 buffering warning/effects preserved;OFF\n")
            } finally { ins.runOnMainSync { PlaybackController.disable(context); home.finish() }; waitFor("service stopped") { !CleanerService.isAlive } }
            UiPrefs.setLanguage(context, "ar")
            val arabicHome = launch(MainActivity::class.java)
            try { screenshot("home-ar-off") } finally { ins.runOnMainSync { arabicHome.finish() } }
        } finally {
            ins.runOnMainSync { PlaybackController.disable(context) }
            waitFor("final stop") { !CleanerService.isAlive }
            ins.runOnMainSync {
                Stage.entries.flatMap { ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(it) }.distinct()
                    .filter { it is MainActivity || it is SetupActivity || it is SettingsActivity }.forEach { it.finish() }
            }
            restore(prefs, saved); restore(uiPrefs, savedUi)
        }
    }
}
