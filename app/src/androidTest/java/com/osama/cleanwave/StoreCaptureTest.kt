package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertFalse
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in hardware screenshots, cropped to the app and excluding system status/navigation bars. */
@RunWith(AndroidJUnit4::class)
class StoreCaptureTest {
    @Test fun captureAppPagesInBothLanguagesWhileOff() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("storeCapture")=="true")
        val ins=InstrumentationRegistry.getInstrumentation(); val context=ins.targetContext
        val prefs=context.getSharedPreferences("cleanwave_ui",Context.MODE_PRIVATE)
        val before=prefs.all.toMap()
        val mode=CleanerPrefs.sourcePreset(context)
        fun views(v:View):List<View> = buildList { add(v); if(v is ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i))) }
        fun launch(type:Class<out Activity>):Activity = ins.startActivitySync(Intent(context,type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)).also {
            ins.waitForIdleSync(); SystemClock.sleep(400)
        }
        fun save(view:View,name:String) {
            ins.waitForIdleSync(); SystemClock.sleep(200)
            val bounds=Rect(); var bars=android.graphics.Insets.NONE
            ins.runOnMainSync {
                val location=IntArray(2); view.getLocationOnScreen(location)
                bounds.set(location[0],location[1],location[0]+view.width,location[1]+view.height)
                bars=view.rootWindowInsets?.getInsets(android.view.WindowInsets.Type.systemBars())?:android.graphics.Insets.NONE
            }
            val screen=requireNotNull(ins.uiAutomation.takeScreenshot())
            check(bounds.intersect(bars.left,bars.top,screen.width-bars.right,screen.height-bars.bottom))
            val image=Bitmap.createBitmap(screen,bounds.left,bounds.top,bounds.width(),bounds.height())
            File(context.getExternalFilesDir("release-04-store"),"$name.png").outputStream().use { image.compress(Bitmap.CompressFormat.PNG,100,it) }
            if(image!==screen)image.recycle(); screen.recycle()
        }
        ins.runOnMainSync { PlaybackController.disable(context) }
        SystemClock.sleep(400); assertFalse(CleanerPrefs.isEnabled(context))
        try {
            UiPrefs.finishSetup(context); UiPrefs.setTheme(context,UiPrefs.THEME_DARK); UiPrefs.setTinted(context,false)
            CleanerPrefs.setSourcePreset(context,2)
            for(locale in listOf("en","ar")) {
                UiPrefs.setLanguage(context,locale)
                val home=launch(MainActivity::class.java)
                try {
                    save(home.window.decorView,"home-$locale")
                    lateinit var dialog:Dialog
                    ins.runOnMainSync { dialog=EnginePerformance.show(home) }
                    try { save(dialog.window!!.decorView,"performance-$locale") }
                    finally { ins.runOnMainSync { dialog.dismiss() } }
                } finally { ins.runOnMainSync { home.finish() } }
                val setup=launch(SetupActivity::class.java)
                try {
                    ins.runOnMainSync {
                        views(setup.window.decorView).filterIsInstance<TextView>().first { it.text==setup.getString(R.string.next) }.performClick()
                    }
                    SystemClock.sleep(400); save(setup.window.decorView,"setup-$locale")
                } finally { ins.runOnMainSync { setup.finish() } }
            }
        } finally {
            CleanerPrefs.setSourcePreset(context,mode)
            prefs.edit().clear().apply {
                before.forEach { (key,value)-> when(value) {
                    is String->putString(key,value); is Int->putInt(key,value); is Boolean->putBoolean(key,value)
                    is Long->putLong(key,value); is Float->putFloat(key,value)
                    is Set<*>->@Suppress("UNCHECKED_CAST") putStringSet(key,value as Set<String>)
                } }
            }.commit()
        }
    }
}
