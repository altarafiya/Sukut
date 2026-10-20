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
    private fun <T : Activity> launch(type: Class<T>): T {
        // Xiaomi blocks an instrumented app's first background Activity launch.
        // Start its ordinary exported launcher as shell, then use in-app launches.
        val command = "am start -W -n ${context.packageName}/com.osama.cleanwave.MainActivity"
        android.os.ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand(command)).bufferedReader().use { it.readText() }
        ins.waitForIdleSync(); SystemClock.sleep(600)
        var activity: Activity? = null
        if (type == MainActivity::class.java) ins.runOnMainSync {
            activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is MainActivity }
        } else activity = ins.startActivitySync(Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
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
        val slider = views(root).filterIsInstance<SeekBar>().first { it.tag == tag }
        if (slider.progress == position) return@runOnMainSync
        assertTrue(slider.performAccessibilityAction(android.view.accessibility.AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id,
            android.os.Bundle().apply { putFloat(android.view.accessibility.AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE, position.toFloat()) }))
    }

    @Test fun customSlidersSetupAndSettings() {
        val prefs = context.getSharedPreferences("cleanwave_state", 0)
        val uiPrefs = context.getSharedPreferences("cleanwave_ui", 0)
        CleanerPrefs.reconcileRuntimeState(context)
        val saved = prefs.all.toMap(); val savedUi = uiPrefs.all.toMap()
        assertFalse("Do not interrupt an existing user session", CleanerPrefs.isEnabled(context))
        val orientation = context.resources.configuration.orientation
        report.appendText("version=${BuildConfig.VERSION_NAME}; custom sliders; orientation=$orientation\n")
        try {
            UiPrefs.finishSetup(context)
            for ((language, theme) in listOf("en" to UiPrefs.THEME_LIGHT, "ar" to UiPrefs.THEME_DARK)) {
                UiPrefs.setLanguage(context, language); UiPrefs.setTheme(context, theme)
                CleanerPrefs.setEnginePerformance(context, false, 1, custom = true)
                val settings = launch(MainActivity::class.java)
                try {
                    click(settings, R.string.engine_performance); SystemClock.sleep(250)
                    val dialog = MainActivity::class.java.getDeclaredField("performanceDialog").apply { isAccessible = true }.get(settings) as Dialog
                    ins.runOnMainSync { views(dialog.window!!.decorView).filterIsInstance<ScrollView>().first().fullScroll(View.FOCUS_DOWN) }
                    ins.waitForIdleSync(); SystemClock.sleep(250)
                    val quality = views(dialog.window!!.decorView).filterIsInstance<SeekBar>().first { it.tag == "custom_output_quality" }
                    val buffer = views(dialog.window!!.decorView).filterIsInstance<SeekBar>().first { it.tag == "custom_audio_buffering" }
                    ins.runOnMainSync {
                        assertEquals(1, quality.max); assertEquals(2, buffer.max)
                        assertTrue(quality.width >= MobileUi(settings).dp(200)); assertTrue(buffer.width >= MobileUi(settings).dp(200))
                        assertEquals(MobileUi(settings).dp(64), quality.height)
                        assertEquals(MobileUi(settings).dp(36), quality.thumb.intrinsicWidth)
                        assertEquals(MobileUi(settings).dp(22), quality.maxHeight)
                        assertEquals(if (language == "ar") View.LAYOUT_DIRECTION_RTL else View.LAYOUT_DIRECTION_LTR, quality.layoutDirection)
                    }
                    screenshot("custom-settings-$orientation-$language-initial")
                    fun drag(target: Int, cancel: Boolean = false) {
                        val bounds = Rect(); ins.runOnMainSync {
                            assertTrue(quality.getGlobalVisibleRect(bounds)); assertEquals(quality.height, bounds.height())
                            // Dialog root coordinates are not display coordinates.
                            val screen = IntArray(2); quality.getLocationOnScreen(screen)
                            bounds.set(screen[0], screen[1], screen[0] + quality.width, screen[1] + quality.height)
                        }
                        val rtl = quality.layoutDirection == View.LAYOUT_DIRECTION_RTL
                        fun x(position: Int): Float = bounds.left + quality.paddingLeft +
                            (quality.width - quality.paddingLeft - quality.paddingRight) * (if (rtl) 1f - position.toFloat() else position.toFloat())
                        val down = SystemClock.uptimeMillis(); val y = bounds.centerY().toFloat()
                        val old = CleanerPrefs.effectsCompatibility(context)
                        fun touch(action: Int, point: Float) { ins.sendPointerSync(MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, point, y, 0)); ins.waitForIdleSync() }
                        touch(MotionEvent.ACTION_DOWN, x(quality.progress)); touch(MotionEvent.ACTION_MOVE, x(target))
                        assertEquals("Changed quality during drag", old, CleanerPrefs.effectsCompatibility(context))
                        assertEquals("Preview did not follow drag", target, quality.progress)
                        touch(if (cancel) MotionEvent.ACTION_CANCEL else MotionEvent.ACTION_UP, x(target))
                        assertEquals("Release/cancel persistence", if (cancel) old else target == 1, CleanerPrefs.effectsCompatibility(context))
                    }
                    drag(1); drag(0, cancel = true)
                    setOption(dialog.window!!.decorView, "custom_output_quality", 0)
                    assertFalse(CleanerPrefs.effectsCompatibility(context))
                    for (index in listOf(0, 1, 2)) {
                        setOption(dialog.window!!.decorView, "custom_audio_buffering", index)
                        assertEquals(index, CleanerPrefs.buffering(context))
                    }
                    assertEquals(EnginePerformanceConfig.CUSTOM, EnginePerformance.selected(context))
                    screenshot("custom-settings-$orientation-$language")
                    ins.runOnMainSync { dialog.dismiss() }
                } finally { ins.runOnMainSync { settings.finish() } }
                val setup = launch(SetupActivity::class.java)
                try {
                    click(setup, R.string.next); SystemClock.sleep(350)
                    click(setup, R.string.next); SystemClock.sleep(350)
                    val quality = tagged(setup, "custom_output_quality") as SeekBar
                    val buffer = tagged(setup, "custom_audio_buffering") as SeekBar
                    assertEquals(0, quality.progress); assertEquals(2, buffer.progress)
                    ins.runOnMainSync { views(setup.window.decorView).filterIsInstance<ScrollView>().first().fullScroll(View.FOCUS_DOWN) }
                    footer(setup, R.string.next); screenshot("custom-setup-$orientation-$language")
                } finally { ins.runOnMainSync { setup.finish() } }
                report.appendText("PASS $language theme=$theme orientation=$orientation: wide colored discrete sliders, RTL, drag preview/release/cancel, accessibility 2/3 options, shared setup persistence/footer\n")
            }
            assertFalse("UI checks started audio", CleanerService.isAlive)
        } finally {
            ins.runOnMainSync {
                Stage.entries.flatMap { ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(it) }.distinct()
                    .filter { it is MainActivity || it is SetupActivity || it is SettingsActivity }.forEach { it.finish() }
            }
            restore(prefs, saved); restore(uiPrefs, savedUi)
        }
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
                    ins.runOnMainSync { tagged(setup, "performance_3").performClick() }
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
                    setOption(dialog.window!!.decorView, "custom_output_quality", 1)
                    waitFor("custom higher quality saved") { CleanerPrefs.effectsCompatibility(context) }
                    setOption(dialog.window!!.decorView, "custom_audio_buffering", 2)
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
                ins.runOnMainSync { tagged(home, "mode_0").performClick() }
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
