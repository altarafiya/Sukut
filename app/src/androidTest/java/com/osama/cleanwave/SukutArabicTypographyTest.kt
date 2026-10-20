package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.content.pm.ActivityInfo
import android.graphics.Bitmap
import android.os.SystemClock
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.StyleSpan
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
class SukutArabicTypographyTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private fun views(view: View): List<View> = buildList {
        add(view)
        if (view is ViewGroup) for (i in 0 until view.childCount) addAll(views(view.getChildAt(i)))
    }
    private fun shot(name: String) {
        ins.waitForIdleSync(); SystemClock.sleep(300)
        val bitmap = ins.uiAutomation.takeScreenshot() ?: error("No screenshot")
        val dir = File(context.filesDir, "arabic-0.3.24").apply { mkdirs() }
        File(dir, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
        bitmap.recycle()
    }
    private fun checkText(root: View) {
        views(root).filterIsInstance<TextView>().filter { ArabicAccentRange.containsArabic(it.text) }.forEach {
            assertEquals("Arabic letter spacing: ${it.text}", 0f, it.letterSpacing, 0f)
            assertTrue(it.includeFontPadding)
            assertFalse(it.paint.isFakeBoldText)
            assertTrue(it.paddingStart > 0 && it.paddingEnd > 0)
            val spans = it.text as? Spanned
            spans?.getSpans(0, spans.length, StyleSpan::class.java)?.forEach { span ->
                assertFalse("No metric-changing emphasis inside Arabic: ${it.text}",
                    ArabicAccentRange.containsArabic(spans.subSequence(spans.getSpanStart(span), spans.getSpanEnd(span))))
            }
            // Each accented Arabic fragment includes its attached article/marks.
            spans?.getSpans(0, spans.length, ForegroundColorSpan::class.java)?.forEach { span ->
                val start = spans.getSpanStart(span); val end = spans.getSpanEnd(span)
                if (ArabicAccentRange.containsArabic(spans.subSequence(start, end))) {
                    val whole = ArabicAccentRange.wholeWords(spans, start, end)
                    assertEquals(whole.first, start); assertEquals(whole.last + 1, end)
                }
            }
        }
    }
    @Test fun performanceAndStartupArabicInBothOrientations() {
        val before = UiPrefs.language(context)
        val wasEnabled = CleanerPrefs.isEnabled(context)
        try {
            UiPrefs.setLanguage(context, "ar")
            for (landscape in listOf(false, true)) {
                val first = ins.startActivitySync(Intent(context, SetupActivity::class.java)
                    .putExtra("page", 2).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                ins.runOnMainSync { first.requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
                ins.waitForIdleSync(); SystemClock.sleep(600)
                lateinit var activity: Activity
                ins.runOnMainSync {
                    activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is SetupActivity }
                    checkText(activity.window.decorView)
                }
                val suffix = if (landscape) "landscape" else "portrait"
                shot("setup-performance-$suffix")
                lateinit var dialog: android.app.Dialog
                ins.runOnMainSync { dialog = EnginePerformance.show(activity) }
                ins.waitForIdleSync()
                ins.runOnMainSync { checkText(dialog.window!!.decorView) }
                shot("performance-dialog-$suffix")
                ins.runOnMainSync { dialog.dismiss(); activity.finish() }
                ins.waitForIdleSync()
                val guide = ins.startActivitySync(Intent(context, SetupActivity::class.java).putExtra("page", 1).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                ins.runOnMainSync { guide.requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
                ins.waitForIdleSync(); SystemClock.sleep(600)
                lateinit var currentGuide: Activity
                ins.runOnMainSync {
                    currentGuide = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is SetupActivity }
                    views(currentGuide.window.decorView).first { it.tag == "shizuku_method_1" }.performClick()
                }
                ins.waitForIdleSync(); SystemClock.sleep(600)
                ins.runOnMainSync {
                    checkText(currentGuide.window.decorView)
                    assertTrue(views(currentGuide.window.decorView).filterIsInstance<TextView>().any { it.text.toString() == currentGuide.getString(R.string.guide_restart_note) })
                }
                shot("setup-shizuku-$suffix")
                ins.runOnMainSync { currentGuide.finish() }
                val welcome = ins.startActivitySync(Intent(context, SetupActivity::class.java).putExtra("page", 0).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                ins.runOnMainSync { welcome.requestedOrientation = if (landscape) ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE else ActivityInfo.SCREEN_ORIENTATION_PORTRAIT }
                ins.waitForIdleSync(); SystemClock.sleep(600)
                ins.runOnMainSync {
                    activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).first { it is SetupActivity }
                    checkText(activity.window.decorView)
                }
                shot("setup-welcome-$suffix")
                ins.runOnMainSync { activity.finish() }
            }
        } finally {
            UiPrefs.setLanguage(context, before)
            ins.runOnMainSync {
                ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED).forEach { it.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED }
            }
            val home = ins.startActivitySync(Intent(context, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            ins.runOnMainSync { if (wasEnabled) PlaybackController.enable(home) }
        }
    }
}
