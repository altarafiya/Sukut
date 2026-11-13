package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.SeekBar
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.Before
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class Beta13UiTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    @Before fun prepareAudioOff() {
        ins.runOnMainSync { PlaybackController.disable(context) }
        val deadline = SystemClock.elapsedRealtime() + 4000
        while ((CleanerPrefs.isEnabled(context) || CleanerService.isAlive) && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
        assertFalse("UI verification leaves Sukut Off", CleanerPrefs.isEnabled(context))
    }
    private fun views(view: View): List<View> = buildList { add(view); if (view is ViewGroup) for (i in 0 until view.childCount) addAll(views(view.getChildAt(i))) }
    private fun shot(name: String) { SystemClock.sleep(150); ins.uiAutomation.takeScreenshot()?.let { bitmap ->
        File(context.getExternalFilesDir("beta16-qa"), "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }; bitmap.recycle()
    } }
    private fun <T : Activity> launch(type: Class<T>): T = (ins.startActivitySync(Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)) as T).also { ins.waitForIdleSync(); SystemClock.sleep(350) }
    @Test fun bufferingIconsAndModeColorsUpdateWhileAudioStaysOff() {
        assertFalse(CleanerPrefs.isEnabled(context))
        val language = UiPrefs.language(context); val mode = CleanerPrefs.sourcePreset(context)
        val buffer = CleanerPrefs.buffering(context); val effects = CleanerPrefs.effectsCompatibility(context); val custom = CleanerPrefs.customPerformance(context)
        try {
            for (locale in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, locale)
                val home = launch(MainActivity::class.java)
                try {
                    for (id in listOf(2, 0, 3)) {
                        ins.runOnMainSync { assertTrue(views(home.window.decorView).first { it.tag == "mode_$id" }.performClick()) }
                        ins.waitForIdleSync()
                        assertEquals(id, CleanerPrefs.sourcePreset(context)); assertFalse(CleanerPrefs.isEnabled(context))
                        ins.runOnMainSync {
                            listOf(2, 0, 3).forEach { mode ->
                                val tile = views(home.window.decorView).first { it.tag == "mode_$mode" }
                                val ink=views(tile).filterIsInstance<ImageView>().first().imageTintList!!.defaultColor
                                assertEquals(if(mode==id)UiPalette(home).accent else UiPalette(home).secondary,ink)
                            }
                        }
                    }
                    for (index in 0..2) {
                        ins.runOnMainSync { CleanerPrefs.setBuffering(context, index) }; ins.waitForIdleSync()
                        ins.runOnMainSync {
                            val row = views(home.window.decorView).first { it.tag == "home_sound_quality" } as LinearLayout
                            val icon = row.getChildAt(0) as ImageView
                            assertEquals(ModeStyle.buffer(index, UiPalette(home).dark), icon.imageTintList!!.defaultColor)
                        }
                    }
                    shot("home-$locale")
                    lateinit var dialog: android.app.Dialog
                    ins.runOnMainSync { dialog = EnginePerformance.show(home) }; ins.waitForIdleSync()
                    try {
                        val shapes = ArrayList<Int>()
                        for (index in 0..2) {
                            ins.runOnMainSync {
                                val root = dialog.window!!.decorView
                                val choice = views(root).first { it.tag == "audio_buffering" } as SegmentedChoice
                                choice.getChildAt(index).performClick()
                            }
                            ins.waitForIdleSync()
                            ins.runOnMainSync {
                                val icon = views(dialog.window!!.decorView).first { it.tag == "buffering_icon" } as ImageView
                                assertEquals(ModeStyle.buffer(index, UiPalette(home).dark), icon.imageTintList!!.defaultColor)
                                // Compare the rendered glyphs using one neutral ink.
                                val image = Bitmap.createBitmap(32, 32, Bitmap.Config.ARGB_8888)
                                icon.drawable.constantState!!.newDrawable().mutate().apply { setTint(-1); setBounds(0, 0, 32, 32); draw(Canvas(image)) }
                                val pixels = IntArray(1024); image.getPixels(pixels, 0, 32, 0, 0, 32, 32); shapes.add(pixels.contentHashCode()); image.recycle()
                            }
                            assertEquals(index, CleanerPrefs.buffering(context)); assertFalse(CleanerPrefs.isEnabled(context))
                            if (locale == "en") shot("performance-$index")
                        }
                        assertEquals(3, shapes.distinct().size)
                        ins.runOnMainSync {
                            val toggle = views(dialog.window!!.decorView).first { it.tag == "phone_effects_switch" } as SukutSwitch
                            val before = toggle.isChecked
                            // CompoundButton can return false without an OnClickListener
                            // even though its checked-change action succeeds.
                            toggle.performClick()
                            assertEquals(!before, toggle.isChecked)
                            assertEquals(!before, CleanerPrefs.effectsCompatibility(context))
                            assertFalse(CleanerPrefs.isEnabled(context))
                        }; ins.waitForIdleSync(); shot("effects-toggle-$locale")
                    } finally { ins.runOnMainSync { dialog.dismiss() } }
                } finally { ins.runOnMainSync { home.finish() } }
                val settings = launch(SettingsActivity::class.java)
                try {
                    ins.runOnMainSync {
                        val slider = views(settings.window.decorView).filterIsInstance<SeekBar>().first()
                        assertTrue("Amplification needs the same 64dp touch height as Optimized", slider.height >= (64 * settings.resources.displayMetrics.density).toInt())
                        slider.requestRectangleOnScreen(android.graphics.Rect(0, 0, slider.width, slider.height), true)
                    }; SystemClock.sleep(120); shot("settings-slider-$locale")
                    ins.runOnMainSync { views(settings.window.decorView).filterIsInstance<android.widget.ScrollView>().first().fullScroll(View.FOCUS_DOWN) }
                    SystemClock.sleep(200); shot("credit-$locale")
                    val monitor=ins.addMonitor(LocalActivationActivity::class.java.name,null,false)
                    ins.runOnMainSync { views(settings.window.decorView).first { it.tag=="settings_audio_worker" }.performClick() }
                    val activation=ins.waitForMonitorWithTimeout(monitor,3500)
                    assertNotNull("Audio worker row must open activation directly",activation)
                    ins.removeMonitor(monitor)
                    ins.runOnMainSync { activation?.finish() }
                } finally { ins.runOnMainSync { settings.finish() } }
            }
        } finally {
            ins.runOnMainSync { CleanerPrefs.setSourcePreset(context, mode); CleanerPrefs.setEnginePerformance(context, effects, buffer, custom) }
            UiPrefs.setLanguage(context, language)
        }
    }
    @Test fun activationGuideIsReadableWithoutStartingPairing() {
        val language = UiPrefs.language(context)
        val method=UiPrefs.activationMethod(context); UiPrefs.setActivationMethod(context,0)
        try {
            for (locale in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, locale)
                val page = launch(LocalActivationActivity::class.java)
                try {
                    ins.runOnMainSync {
                        val all = views(page.window.decorView)
                        assertTrue(all.filterIsInstance<android.widget.EditText>().isEmpty())
                        assertTrue(all.any { it.tag=="guide_phone" })
                        assertFalse(all.any { it.tag=="guide_windows" })
                    }
                    ins.waitForIdleSync(); shot("activation-phone-$locale")
                    ins.runOnMainSync { views(page.window.decorView).first { it.tag=="guide_choose_computer" }.performClick() }
                    ins.waitForIdleSync()
                    ins.runOnMainSync {
                        val all=views(page.window.decorView)
                        assertTrue(all.any { it.tag=="guide_windows" }); assertFalse(all.any { it.tag=="guide_phone" })
                    }; shot("activation-windows-$locale")
                    ins.runOnMainSync { views(page.window.decorView).first { it.tag=="guide_choose_phone" }.performClick() }
                    ins.waitForIdleSync()
                    ins.runOnMainSync {
                        assertFalse("Reading the guide must not start pairing",LocalActivationState.busy)
                        assertFalse("Reading guide closed: locale=$locale",page.isFinishing)
                        repeat(3) {
                            views(page.window.decorView).filterIsInstance<android.widget.TextView>().first { it.isClickable && it.text==page.getString(R.string.next) }.performClick()
                        }
                        views(page.window.decorView).first { it.tag=="guide_picture_wireless" }.performClick()
                    }
                    val pictureDeadline=SystemClock.elapsedRealtime()+3000
                    while(ins.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(page.getString(R.string.guide_photo_note))?.isNotEmpty()!=true && SystemClock.elapsedRealtime()<pictureDeadline)SystemClock.sleep(80)
                    assertTrue("The guide picture viewer must open",ins.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(page.getString(R.string.guide_photo_note))?.isNotEmpty()==true)
                    shot("guide-photo-$locale")
                    val done=ins.uiAutomation.rootInActiveWindow?.findAccessibilityNodeInfosByText(page.getString(R.string.performance_done))?.firstOrNull()
                    assertTrue("Close the picture using its fixed Done action",done?.performAction(android.view.accessibility.AccessibilityNodeInfo.ACTION_CLICK)==true)
                    SystemClock.sleep(250); ins.waitForIdleSync()
                    assertFalse("Dismissing the picture must leave its guide open",page.isFinishing)
                    assertFalse("Reading instructions must not activate audio", CleanerPrefs.isEnabled(context))
                } finally { ins.runOnMainSync { page.finish() } }
            }
        } finally { UiPrefs.setLanguage(context, language); UiPrefs.setActivationMethod(context,method) }
    }
    @Test fun whiteTintDoesNotTurnControlsWhite() {
        val tint=UiPrefs.tintColor(context); val tinted=UiPrefs.tinted(context)
        try {
            UiPrefs.setTintColor(context,-1); UiPrefs.setTinted(context,true)
            val blue=UiPalette(context).accent
            assertTrue((blue and 255) > (blue ushr 16 and 255)+20)
        } finally { UiPrefs.setTintColor(context,tint); UiPrefs.setTinted(context,tinted) }
    }
}
