package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.os.Debug
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleCallback
import androidx.test.runner.lifecycle.Stage
import java.io.FileInputStream
import java.util.concurrent.atomic.AtomicReference
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

/** On-device layout review only. Never starts audio or records sound. */
@RunWith(AndroidJUnit4::class)
class SukutBetaUiReviewTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context get() = ins.targetContext
    private val evidence get() = File(context.getExternalFilesDir(null), "beta2-review").apply { mkdirs() }

    @Test fun reviewHomePickerAndSetupInBothLanguages() {
        assertFalse("Turn audio OFF before this layout-only review", CleanerPrefs.isEnabled(context))
        val language = UiPrefs.language(context); val theme = UiPrefs.theme(context)
        val effects = CleanerPrefs.effectsCompatibility(context); val buffer = CleanerPrefs.buffering(context)
        val custom = CleanerPrefs.customPerformance(context)
        try {
            for (locale in listOf("en", "ar")) {
                EnginePerformance.select(context, EnginePerformanceConfig.BALANCED)
                UiPrefs.setLanguage(context, locale)
                UiPrefs.setTheme(context, UiPrefs.THEME_DARK)
                val home = launch(MainActivity::class.java)
                try {
                    assertTextFits(home.window.decorView)
                    screenshot("home-$locale")
                    var picker: android.app.Dialog? = null
                    ins.runOnMainSync { picker = EnginePerformance.show(home) }
                    settle()
                    assertTextFits(picker!!.window!!.decorView)
                    screenshot("performance-$locale")
                    val tags = setOf("phone_effects_switch", "audio_buffering")
                    val originalRows = views(picker!!.window!!.decorView).filter { it.tag in tags }
                    assertEquals(2, originalRows.size)
                    assertTrue(originalRows.first { it.tag == "phone_effects_switch" } is SukutSwitch)
                    ins.runOnMainSync {
                        (originalRows.first { it.tag == "phone_effects_switch" } as android.widget.Switch).performClick()
                        (originalRows.first { it.tag == "audio_buffering" } as SegmentedChoice).getChildAt(2).performClick()
                    }
                    settle()
                    val currentRows = views(picker!!.window!!.decorView).filter { it.tag in tags }
                    assertEquals("Changing options recreated controls", originalRows, currentRows)
                    assertTrue(CleanerPrefs.effectsCompatibility(context)); assertEquals(2, CleanerPrefs.buffering(context))
                    assertFalse(CleanerPrefs.isEnabled(context)); assertFalse(CleanerPrefs.startPending(context))
                    assertTextFits(picker!!.window!!.decorView)
                    screenshot("playback-controls-$locale")
                    ins.runOnMainSync { picker!!.dismiss() }
                } finally { ins.runOnMainSync { home.finish() } }
                val setup = launch(SetupActivity::class.java, 2)
                try {
                    assertTextFits(setup.window.decorView)
                    screenshot("setup-performance-$locale")
                } finally { ins.runOnMainSync { setup.finish() } }
            }
            val memory = Debug.MemoryInfo(); Debug.getMemoryInfo(memory)
            File(evidence, "review.txt").writeText("version=${BuildConfig.VERSION_NAME}\nlocale=en,ar\n" +
                "text_fit=PASS\npicker_views_reused=PASS\nno_audio_started=true\n" +
                "ui_process_pss_kb=${memory.totalPss}\nSingle observation after UI review; not a battery or RAM improvement benchmark.\n")
        } finally {
            UiPrefs.setLanguage(context, language); UiPrefs.setTheme(context, theme)
            CleanerPrefs.setEnginePerformance(context, effects, buffer, custom)
        }
    }
    @Test fun selectingModesWhileOffDoesNotStartAudio() {
        assertFalse("Leave an active session undisturbed", CleanerPrefs.isEnabled(context))
        assertFalse(CleanerService.isAlive)
        val preset = CleanerPrefs.sourcePreset(context)
        val setup = UiPrefs.setupCompleted(context)
        UiPrefs.finishSetup(context)
        val home = launch(MainActivity::class.java)
        try {
            for (mode in AudioModes.filtering) {
                ins.runOnMainSync { views(home.window.decorView).first { it.tag == "mode_${mode.id}" }.performClick() }
                settle()
                assertEquals(mode.id, CleanerPrefs.sourcePreset(context))
                assertFalse("Mode selection started processing", CleanerPrefs.isEnabled(context))
                assertFalse("Mode selection queued a service start", CleanerPrefs.startPending(context))
                assertFalse(CleanerService.isAlive)
                val reconnect = MainActivity::class.java.getDeclaredField("reconnectDialog").apply { isAccessible = true }.get(home)
                assertNull("Mode selection opened worker setup", reconnect)
            }
        } finally {
            CleanerPrefs.setSourcePreset(context, preset)
            context.getSharedPreferences("cleanwave_ui", 0).edit().putBoolean("sukut_setup_v1", setup).commit()
            ins.runOnMainSync { home.finish() }
        }
    }

    @Test fun sliderOwnsTheWholeDragEvenWithVerticalDrift() {
        assertFalse("Leave an active session undisturbed", CleanerPrefs.isEnabled(context))
        val home = launch(MainActivity::class.java)
        try { ins.runOnMainSync {
            val ui = MobileUi(home)
            val slider = TouchSeekBar(home).apply { max = 100; progress = 30; minimumHeight = ui.dp(64) }
            var committed = 0
            slider.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: android.widget.SeekBar?, value: Int, user: Boolean) {}
                override fun onStartTrackingTouch(bar: android.widget.SeekBar?) {}
                override fun onStopTrackingTouch(bar: android.widget.SeekBar?) { committed++ }
            })
            val column = android.widget.LinearLayout(home).apply {
                orientation = android.widget.LinearLayout.VERTICAL
                addView(ui.space(100)); addView(slider, android.widget.LinearLayout.LayoutParams(-1, ui.dp(64))); addView(ui.space(1800))
            }
            val scroll = BounceScrollView(home).apply { addView(column) }
            val width = ui.dp(340); val height = ui.dp(500)
            scroll.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY))
            scroll.layout(0, 0, width, height)
            val down = SystemClock.uptimeMillis(); val y = slider.top + slider.height / 2f
            fun touch(action: Int, x: Float, atY: Float) {
                val event = android.view.MotionEvent.obtain(down, SystemClock.uptimeMillis(), action, x, atY, 0)
                scroll.dispatchTouchEvent(event); event.recycle()
            }
            touch(android.view.MotionEvent.ACTION_DOWN, width * .3f, y)
            touch(android.view.MotionEvent.ACTION_MOVE, width * .6f, y - ui.dp(90))
            touch(android.view.MotionEvent.ACTION_MOVE, width * .85f, y - ui.dp(110))
            assertEquals("Parent stole the slider gesture", 0, scroll.scrollY)
            assertTrue("Slider stopped following the finger", slider.progress > 60)
            assertEquals("Drag committed before release", 0, committed)
            touch(android.view.MotionEvent.ACTION_UP, width * .85f, y - ui.dp(110))
            assertEquals(1, committed)
            assertEquals("Custom bounce translated the whole page", 0f, column.translationY, 0f)
        } } finally { ins.runOnMainSync { home.finish() } }
    }

    @Test fun neutralAppearanceEqualControlsAndIllustratedHelp() {
        assertFalse("Leave an active session undisturbed", CleanerPrefs.isEnabled(context))
        val language = UiPrefs.language(context); val theme = UiPrefs.theme(context)
        val tinted = UiPrefs.tinted(context); val color = UiPrefs.tintColor(context)
        val setup = UiPrefs.setupCompleted(context)
        UiPrefs.finishSetup(context)
        try {
            for (locale in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, locale); UiPrefs.setTheme(context, UiPrefs.THEME_DARK); UiPrefs.setTinted(context, false)
                val home = launch(MainActivity::class.java)
                try {
                    ins.runOnMainSync {
                        assertEquals(android.graphics.Color.BLACK, UiPalette(home).background)
                        assertFalse(views(home.window.decorView).any { it.tag == "mode_tip" })
                        val modes = views(home.window.decorView).filter { it.tag?.toString() in setOf("mode_0", "mode_2", "mode_3") }
                        assertEquals(3, modes.size); assertEquals("Mode tiles differ in height", 1, modes.map { it.height }.distinct().size)
                        assertTrue(views(home.window.decorView).first { it.tag == "home_mode_help" }.performClick())
                    }
                    settle()
                    val modeHelp = MainActivity::class.java.getDeclaredField("modeHelpDialog").apply { isAccessible = true }.get(home) as android.app.Dialog
                    assertFalse(modeHelp is android.app.AlertDialog)
                    assertPinnedAction(modeHelp)
                    assertTextFits(modeHelp.window!!.decorView); screenshot("mode-help-$locale")
                    ins.runOnMainSync { modeHelp.dismiss() }
                    var help: android.app.Dialog? = null
                    ins.runOnMainSync { help = AudioHelp.showPerformance(home) }; settle()
                    val sections = views(help!!.window!!.decorView).first { it.tag == "performance_help_sections" } as ViewGroup
                    assertEquals(3, sections.childCount); assertFalse(help is android.app.AlertDialog)
                    assertPinnedAction(help!!)
                    assertTextFits(help!!.window!!.decorView); screenshot("playback-help-$locale")
                    ins.runOnMainSync { help!!.dismiss() }
                    var picker: android.app.Dialog? = null
                    ins.runOnMainSync { picker = TintColorPicker.show(home, color) { fail("Cancel applied a colour") } }; settle()
                    ins.runOnMainSync {
                        val hex = views(picker!!.window!!.decorView).first { it.tag == "tint_hex" } as android.widget.EditText
                        hex.setText("#CC8844"); picker!!.dismiss()
                        assertEquals("Picker draft escaped before Apply", color, UiPrefs.tintColor(context))
                    }
                } finally { ins.runOnMainSync { home.finish() } }
                val settings = launch(SettingsActivity::class.java)
                try {
                    ins.runOnMainSync {
                        assertTrue(views(settings.window.decorView).first { it.tag == "brand_icon" }.clipToOutline)
                        for (tag in listOf("appearance_choice", "language_choice")) {
                            val choice = views(settings.window.decorView).first { it.tag == tag } as ViewGroup
                            val heights = (0 until choice.childCount).map { choice.getChildAt(it).height }
                            assertEquals("Uneven segments: $tag", 1, heights.distinct().size)
                        }
                    }
                    assertTextFits(settings.window.decorView); screenshot("settings-equal-controls-$locale")
                } finally { ins.runOnMainSync { settings.finish() } }
            }
        } finally {
            UiPrefs.setLanguage(context, language); UiPrefs.setTheme(context, theme)
            UiPrefs.setTinted(context, tinted); UiPrefs.setTintColor(context, color)
            context.getSharedPreferences("cleanwave_ui", 0).edit().putBoolean("sukut_setup_v1", setup).commit()
        }
    }

    private fun assertPinnedAction(dialog: android.app.Dialog) = ins.runOnMainSync {
        val all = views(dialog.window!!.decorView)
        val button = all.first { it.tag == "help_done" }
        var ancestor = button.parent
        while (ancestor is View) {
            assertFalse("Done scrolls with the explanation", ancestor is android.widget.ScrollView)
            ancestor = ancestor.parent
        }
        val bounds = android.graphics.Rect(); assertTrue(button.getGlobalVisibleRect(bounds))
        assertEquals("Done is clipped", button.height, bounds.height())
        val top = bounds.top
        val scroll = all.filterIsInstance<BounceScrollView>().single()
        scroll.scrollTo(0, scroll.getChildAt(0).height)
        assertTrue(button.getGlobalVisibleRect(bounds)); assertEquals("Done moved with the body", top, bounds.top)
        val scrollBounds = android.graphics.Rect(); val footerBounds = android.graphics.Rect()
        assertTrue(scroll.getGlobalVisibleRect(scrollBounds)); assertTrue(all.first { it.tag == "sheet_footer" }.getGlobalVisibleRect(footerBounds))
        assertTrue("Body overlaps the fixed actions", scrollBounds.bottom <= footerBounds.top)
    }

    private fun <T : Activity> launch(type: Class<T>, page: Int = 0): T {
        // Xiaomi rejects background launches from an instrumented UID. Start the
        // exported Home via ADB, then open internal pages from that visible app.
        val observed = AtomicReference<T>()
        val monitor = ActivityLifecycleMonitorRegistry.getInstance()
        val listener = ActivityLifecycleCallback { activity, stage ->
            if (stage == Stage.RESUMED) activity.window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            if (type.isInstance(activity) && stage == Stage.RESUMED) observed.set(type.cast(activity))
        }
        ins.runOnMainSync { monitor.addLifecycleCallback(listener) }
        try {
            android.os.ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand(
                "am start -W -f 0x10008000 -n ${context.packageName}/${MainActivity::class.java.name}")).use { it.readBytes() }
            if (type != MainActivity::class.java) {
                ins.runOnMainSync { context.startActivity(Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK).putExtra("page", page)) }
            }
            val until = SystemClock.elapsedRealtime() + 5000
            while (observed.get() == null && SystemClock.elapsedRealtime() < until) SystemClock.sleep(50)
            settle(); return observed.get() ?: error("Activity not resumed: ${type.name}")
        } finally { ins.runOnMainSync { monitor.removeLifecycleCallback(listener) } }
    }
    private fun settle() { ins.waitForIdleSync(); SystemClock.sleep(300); ins.waitForIdleSync() }
    private fun views(view: View): List<View> = buildList {
        add(view); if (view is ViewGroup) for (i in 0 until view.childCount) addAll(views(view.getChildAt(i)))
    }
    private fun assertTextFits(root: View) = ins.runOnMainSync {
        for (view in views(root).filterIsInstance<TextView>().filter { it.isShown && it.text.isNotBlank() }) {
            val layout = view.layout ?: continue
            val last = layout.lineCount - 1
            assertTrue("No lines: ${view.text}", last >= 0)
            assertEquals("Ellipsized: ${view.text}", 0, layout.getEllipsisCount(last))
            assertTrue("Clipped vertically: ${view.text}", layout.getLineBottom(last) <=
                view.height - view.compoundPaddingTop - view.compoundPaddingBottom)
        }
    }
    private fun screenshot(name: String) {
        val bitmap = ins.uiAutomation.takeScreenshot() ?: error("Screenshot unavailable")
        File(evidence, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }
}
