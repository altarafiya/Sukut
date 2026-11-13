package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class Release04UiTest {
    private val ins=InstrumentationRegistry.getInstrumentation()
    private val context=ins.targetContext
    private fun views(v:View):List<View> = buildList { add(v); if(v is ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i))) }
    private fun launch(type:Class<out Activity>)=ins.startActivitySync(Intent(context,type).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
    @Test fun setupProgressStaysOutsideTheSlidingPagesAndNoMoreOptionsRemain() {
        val language=UiPrefs.language(context); val method=UiPrefs.activationMethod(context)
        UiPrefs.setLanguage(context,"en"); UiPrefs.setActivationMethod(context,0)
        val page=launch(SetupActivity::class.java)
        try {
            lateinit var header:View; lateinit var slides:View
            ins.runOnMainSync {
                header=views(page.window.decorView).first { it.tag=="setup_progress_header" }
                slides=views(page.window.decorView).first { it.tag=="setup_pages" }
                assertFalse(views(slides).contains(header))
                views(page.window.decorView).filterIsInstance<TextView>().first { it.text==page.getString(R.string.next) }.performClick()
                assertSame(header,views(page.window.decorView).first { it.tag=="setup_progress_header" })
                assertEquals(0f,header.translationX); assertEquals(1f,header.alpha)
                assertEquals(page.getString(R.string.setup_worker_title),(views(page.window.decorView).first { it.tag=="setup_step_counter" } as TextView).text)
                assertFalse(views(page.window.decorView).any { it.tag=="guide_more_options" })
                assertTrue(views(page.window.decorView).first { it.tag=="guide_choose_phone" }.isSelected)
                assertFalse(views(page.window.decorView).any { it.tag=="guide_step_counter" })
            }
            SystemClock.sleep(350)
            ins.runOnMainSync { assertEquals(0f,header.translationX); assertEquals(0f,slides.translationX) }
        } finally { ins.runOnMainSync { page.finish() }; UiPrefs.setLanguage(context,language); UiPrefs.setActivationMethod(context,method) }
    }
    @Test fun offAndStandbyHaveDifferentFillsAndSymbols() {
        val page=launch(MainActivity::class.java)
        try {
            ins.runOnMainSync {
                fun draw(state:PowerButton.State):Bitmap {
                    val button=PowerButton(page,UiPalette(page)).apply { show(state,state.name) }
                    button.measure(View.MeasureSpec.makeMeasureSpec(240,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(240,View.MeasureSpec.EXACTLY))
                    button.layout(0,0,240,240)
                    val info=AccessibilityNodeInfo.obtain(); button.onInitializeAccessibilityNodeInfo(info)
                    assertEquals(state==PowerButton.State.STANDBY,info.isChecked)
                    return Bitmap.createBitmap(240,240,Bitmap.Config.ARGB_8888).apply { button.draw(Canvas(this)) }
                }
                val off=draw(PowerButton.State.OFF); val standby=draw(PowerButton.State.STANDBY)
                val a=IntArray(240*240); val b=IntArray(a.size)
                off.getPixels(a,0,240,0,0,240,240); standby.getPixels(b,0,240,0,0,240,240)
                assertTrue(a.indices.count { a[it]!=b[it] }>a.size/4)
                listOf("power-off" to off,"power-standby" to standby).forEach { (name,bitmap) ->
                    File(context.getExternalFilesDir("release-04-qa"),"$name.png").outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG,100,it) }; bitmap.recycle()
                }
            }
        } finally { ins.runOnMainSync { page.finish() } }
    }
}
