package com.osama.cleanwave

import android.animation.ValueAnimator
import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.ScrollView
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class SukutUiPhoneTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context = ins.targetContext
    private val report get() = File(context.filesDir, "ui-phone-checks.txt")

    @Test fun widerButtonsAndSetupSlidesInBothLanguages() {
        report.appendText("version=${BuildConfig.VERSION_NAME}\n")
        val language = UiPrefs.language(context); val theme = UiPrefs.theme(context)
        try {
            for (locale in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, locale); UiPrefs.setTheme(context, UiPrefs.THEME_SYSTEM)
                val home = launch(MainActivity::class.java)
                try {
                    val buttons = views(home.window.decorView).filter { it.isClickable && it.contentDescription?.toString() in AudioModes.modes.map { m -> home.getString(m.label) } }
                    assertEquals(4, buttons.size)
                    assertTrue("Mode buttons remain narrow", buttons.all { it.width > home.resources.displayMetrics.widthPixels * .3 })
                    screenshot("home-$locale-system")
                } finally { ins.runOnMainSync { home.finish() } }
                val setup = launch(SetupActivity::class.java)
                try {
                    screenshot("setup-$locale-welcome")
                    click(setup, R.string.next)
                    SystemClock.sleep(80)
                    if (ValueAnimator.areAnimatorsEnabled()) ins.runOnMainSync {
                        val pages = views(setup.window.decorView).filterIsInstance<ScrollView>()
                        assertEquals("Setup did not slide between pages", 2, pages.size)
                        val translation = pages.last().translationX
                        assertTrue("Incorrect $locale slide direction: $translation", if (locale == "ar") translation < 0 else translation > 0)
                        report.appendText("$locale forward translation=$translation\n")
                    }
                    SystemClock.sleep(350)
                    screenshot("setup-$locale-shizuku")
                    click(setup, R.string.back)
                    SystemClock.sleep(80)
                    if (ValueAnimator.areAnimatorsEnabled()) ins.runOnMainSync {
                        val pages = views(setup.window.decorView).filterIsInstance<ScrollView>()
                        assertEquals(2, pages.size)
                        val translation = pages.last().translationX
                        assertTrue("Incorrect $locale return slide direction: $translation", if (locale == "ar") translation > 0 else translation < 0)
                        report.appendText("$locale back translation=$translation\n")
                    }
                    SystemClock.sleep(350)
                    ins.runOnMainSync { assertEquals(1, views(setup.window.decorView).filterIsInstance<ScrollView>().size) }
                } finally { ins.runOnMainSync { setup.finish() } }
            }
            UiPrefs.setLanguage(context, "en"); UiPrefs.setTheme(context, UiPrefs.THEME_DARK)
            val dark = launch(MainActivity::class.java)
            try { screenshot("home-en-dark") } finally { ins.runOnMainSync { dark.finish() } }
            report.appendText("UI widths, EN/AR forward/back transitions and page disposal passed\n")
        } finally { UiPrefs.setLanguage(context, language); UiPrefs.setTheme(context, theme) }
    }

    @Test fun bufferingChangesRestartAndOffCancelsARapidChange() {
        report.appendText("buffer restart verification\n")
        val original = CleanerPrefs.buffering(context)
        val originalPreset = CleanerPrefs.sourcePreset(context)
        val originalLanguage = UiPrefs.language(context)
        UiPrefs.setLanguage(context, "en")
        CleanerPrefs.setSourcePreset(context, CleanerPrefs.SOURCE_MUSIC_FILTER)
        val main = launch(MainActivity::class.java)
        try {
            ins.runOnMainSync { CleanerPrefs.setBuffering(context, 1); PlaybackController.enable(context) }
            waitFor { CleanerPrefs.status(context) == "Active" && CleanerService.routeGeneration > 0 }
            val notifications = context.getSystemService(android.app.NotificationManager::class.java).activeNotifications
            assertEquals("Audio Mode: Music Filter", notifications.first { it.id == CleanerService.NOTIFICATION_ID }.notification.extras.getCharSequence(android.app.Notification.EXTRA_TEXT).toString())
            val first = CleanerService.routeGeneration
            ins.runOnMainSync { CleanerPrefs.setBuffering(context, 2) }
            waitFor { CleanerService.routeGeneration > first && CleanerPrefs.status(context) == "Active" }
            val second = CleanerService.routeGeneration
            ins.runOnMainSync { CleanerPrefs.setBuffering(context, 2) }
            SystemClock.sleep(1200)
            assertEquals("Unchanged buffer restarted processing", second, CleanerService.routeGeneration)
            ins.runOnMainSync { CleanerPrefs.setBuffering(context, 0); CleanerPrefs.setBuffering(context, 1); PlaybackController.disable(context) }
            waitFor { !CleanerService.isAlive }
            SystemClock.sleep(1500)
            assertFalse("A pending restart re-enabled processing after OFF", CleanerPrefs.isEnabled(context))
            report.appendText("buffer 1→2 restarted $first→$second; same selection stable; OFF canceled rapid changes\n")
        } finally {
            ins.runOnMainSync { PlaybackController.disable(context) }
            waitFor { !CleanerService.isAlive }
            CleanerPrefs.setBuffering(context, original)
            CleanerPrefs.setSourcePreset(context, originalPreset)
            UiPrefs.setLanguage(context, originalLanguage)
            ins.runOnMainSync { main.finish() }
        }
    }

    @Test fun readinessSharesOneHeaderLineInEnglishAndArabic() {
        val language = UiPrefs.language(context)
        try {
            for (locale in listOf("en", "ar")) {
                UiPrefs.setLanguage(context, locale)
                val settings = launch(SettingsActivity::class.java)
                try {
                    ins.runOnMainSync {
                        val all = views(settings.window.decorView).filterIsInstance<TextView>()
                        val ready = all.first { it.text.toString() == settings.getString(R.string.shizuku_running_shell) }
                        val title = all.first { it.text.toString() == settings.getString(R.string.shizuku) }
                        assertEquals("Readiness is on a different row", title.parent, ready.parent)
                        assertEquals(1, ready.lineCount)
                        (ready.parent as View).requestRectangleOnScreen(android.graphics.Rect(0, 0, (ready.parent as View).width, ready.height), true)
                    }
                    SystemClock.sleep(350); screenshot("settings-$locale-ready")
                } finally { ins.runOnMainSync { settings.finish() } }
            }
            report.appendText("Ready shares Shizuku header in EN/AR; one line\n")
        } finally { UiPrefs.setLanguage(context, language) }
    }

    private fun <T : Activity> launch(type: Class<T>): T = (ins.startActivitySync(Intent(context, type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as T).also {
        ins.waitForIdleSync(); SystemClock.sleep(400)
    }
    private fun views(view: View): List<View> = buildList {
        add(view); if (view is ViewGroup) for (i in 0 until view.childCount) addAll(views(view.getChildAt(i)))
    }
    private fun click(activity: Activity, id: Int) = ins.runOnMainSync {
        var view: View = views(activity.window.decorView).filterIsInstance<TextView>().first { it.text.toString() == activity.getString(id) }
        while (!view.isClickable) view = view.parent as View
        assertTrue(view.performClick())
    }
    private fun waitFor(condition: () -> Boolean) {
        val until = SystemClock.elapsedRealtime() + 15000
        while (!condition() && SystemClock.elapsedRealtime() < until) SystemClock.sleep(100)
        assertTrue("Timed out: ${CleanerPrefs.status(context)} / ${CleanerPrefs.error(context)}", condition())
    }
    private fun screenshot(name: String) {
        ins.uiAutomation.takeScreenshot()?.let { bitmap ->
            File(context.filesDir, "$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }; bitmap.recycle()
        }
    }
}
