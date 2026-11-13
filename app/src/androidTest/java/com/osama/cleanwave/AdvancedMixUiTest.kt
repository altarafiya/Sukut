package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import java.io.File
import org.junit.Assume.assumeTrue
import android.os.Bundle
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.EditText
import android.widget.SeekBar
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith

/** UI/configuration only. No worker activation or audio playback. */
@RunWith(AndroidJUnit4::class)
class AdvancedMixUiTest {
    private val ins=InstrumentationRegistry.getInstrumentation()
    private val context=ins.targetContext
    private fun views(v:View):List<View> = buildList { add(v);if(v is ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i))) }
    private fun all(page:Activity)=views(page.window.decorView)
    private inline fun isolated(language:String="en",block:(MainActivity)->Unit) {
        assumeFalse(CleanerPrefs.isEnabled(context) || CleanerService.isAlive)
        val names=listOf("cleanwave_ui","cleanwave_state","advanced_mix_access")
        val snapshots=names.associateWith { context.getSharedPreferences(it,Context.MODE_PRIVATE).all.toMap() }
        context.getSharedPreferences("advanced_mix_access",Context.MODE_PRIVATE).edit().clear().commit()
        UiPrefs.setLanguage(context,language);UiPrefs.finishSetup(context)
        context.getSharedPreferences("cleanwave_state",Context.MODE_PRIVATE).edit()
            .putBoolean("notification_prompted_sukut",true).putBoolean("quick_tile_prompted_v1",true).commit()
        CleanerPrefs.setAdvancedMix(context,false);CleanerPrefs.setSourcePreset(context,CleanerPrefs.SOURCE_OPTIMIZED)
        val page=ins.startActivitySync(Intent(context,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as MainActivity
        ins.waitForIdleSync();SystemClock.sleep(400)
        try { block(page) } finally {
            ins.runOnMainSync { page.finish() };ins.waitForIdleSync()
            for(name in names) {
                val edit=context.getSharedPreferences(name,Context.MODE_PRIVATE).edit().clear()
                for((key,value) in snapshots.getValue(name))when(value) {
                    is Boolean->edit.putBoolean(key,value);is Int->edit.putInt(key,value);is Long->edit.putLong(key,value)
                    is Float->edit.putFloat(key,value);is String->edit.putString(key,value)
                    is Set<*>->edit.putStringSet(key,value.filterIsInstance<String>().toSet())
                }
                edit.commit()
            }
        }
    }
    // The real code is not in the source: pass it with -e advancedCode <code>.
    private fun advancedCode():String = InstrumentationRegistry.getArguments().getString("advancedCode").also {
        org.junit.Assume.assumeTrue("Pass -e advancedCode to test the real code", it != null)
    }!!
    private fun enter(dialog:Dialog,code:String) {
        ins.runOnMainSync {
            val nodes=views(dialog.window!!.decorView)
            nodes.filterIsInstance<EditText>().first { it.tag=="advanced_mix_code" }.setText(code)
            nodes.filterIsInstance<TextView>().first { it.isClickable && it.text==dialog.context.getString(R.string.advanced_enable) }.performClick()
        }
        ins.waitForIdleSync()
    }
    @Test fun thirdWrongEntryClosesDialogAndReopeningCannotBypassTheLock() = isolated { page ->
        var dialog:Dialog?=null
        ins.runOnMainSync { dialog=AdvancedMixDialog.show(page){} }
        enter(dialog!!,"incorrect");enter(dialog!!,"incorrect")
        ins.runOnMainSync { dialog!!.dismiss();dialog=AdvancedMixDialog.show(page){} }
        enter(dialog!!,"incorrect")
        ins.runOnMainSync {
            assertFalse(dialog!!.isShowing);assertNull(AdvancedMixDialog.show(page){})
            assertFalse(CleanerPrefs.advancedMix(context))
            val until=context.getSharedPreferences("advanced_mix_access",Context.MODE_PRIVATE).getLong("locked_until",0)
            assertTrue(until-System.currentTimeMillis() in 590000L..600000L)
        }
    }
    @Test fun correctCodeRevealsThreeSlidersWithoutStartingAudioAndOtherModesKeepTheirPresets() = isolated { page ->
        var dialog:Dialog?=null
        val originalBackground=CleanerPrefs.optimizedBackground(context)
        ins.runOnMainSync { dialog=AdvancedMixDialog.show(page){} }
        enter(dialog!!,advancedCode())
        ins.waitForIdleSync();SystemClock.sleep(150)
        ins.runOnMainSync {
            assertFalse(dialog!!.isShowing);assertTrue(CleanerPrefs.advancedMix(context))
            assertEquals(0,CleanerPrefs.sourceVoiceDb(context));assertEquals(-20,CleanerPrefs.sourceMusicDb(context))
            assertEquals(originalBackground,CleanerPrefs.sourceOtherDb(context))
            val sliders=(0..2).map { stem->all(page).filterIsInstance<SeekBar>().first { it.tag=="advanced_mix_$stem" } }
            assertTrue(sliders.all { it.isShown && it.max==30 })
            for((index,progress) in listOf(10,20,0).withIndex()) {
                val args=Bundle().apply {putFloat(AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,progress.toFloat())}
                assertTrue(sliders[index].performAccessibilityAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id,args))
            }
            assertEquals(-10,CleanerPrefs.sourceVoiceDb(context));assertEquals(0,CleanerPrefs.sourceMusicDb(context));assertEquals(-20,CleanerPrefs.sourceOtherDb(context))
            CleanerPrefs.setSourcePreset(context,CleanerPrefs.SOURCE_STRICT)
            assertEquals(0,CleanerPrefs.sourceVoiceDb(context));assertEquals(-100,CleanerPrefs.sourceMusicDb(context));assertEquals(-100,CleanerPrefs.sourceOtherDb(context))
            assertFalse(CleanerPrefs.isEnabled(context));assertFalse(CleanerService.isAlive)
        }
    }
    @Test fun aShortPressCancelsAndAContinuousFiveSecondHoldOpensTheCustomDialog() = isolated { page ->
        lateinit var target:OptimizedHoldLayout
        var dialog:Dialog?=null;var fired=0;var clicked=0;var firedAt=0L
        ins.runOnMainSync {
            assertTrue("The isolated emulator must have no system dialog covering the app",page.hasWindowFocus())
            target=OptimizedHoldLayout(page){fired++;firedAt=SystemClock.uptimeMillis();dialog=AdvancedMixDialog.show(page){}}
            target.isClickable=true;target.setOnClickListener {clicked++}
            page.addContentView(target,ViewGroup.LayoutParams(120,120))
        }
        fun event(action:Int,down:Long) {ins.runOnMainSync {
            val e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,60f,60f,0);target.dispatchTouchEvent(e);e.recycle()
        }}
        val short=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,short);SystemClock.sleep(100);event(MotionEvent.ACTION_UP,short)
        val held=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,held)
        SystemClock.sleep(4500);assertEquals(0,fired)
        SystemClock.sleep(750);ins.waitForIdleSync()
        try {ins.runOnMainSync {assertEquals(1,fired);assertTrue(firedAt-held>=5000);assertTrue(dialog!!.isShowing)}}
        finally {event(MotionEvent.ACTION_UP,held);ins.runOnMainSync {dialog?.dismiss();(target.parent as ViewGroup).removeView(target)}}
        assertEquals("The completed hold must not also click",1,clicked)
    }
    @Test fun threeSecondHoldDisablesAdvancedWithoutDialogAndRestoresStandardMix() = isolated {page->
        ins.runOnMainSync {CleanerPrefs.setAdvancedMix(context,true)}
        ins.waitForIdleSync();SystemClock.sleep(150)
        val target=all(page).first {it.tag=="mode_${CleanerPrefs.SOURCE_OPTIMIZED}"} as OptimizedHoldLayout
        fun event(action:Int,down:Long) {ins.runOnMainSync {
            val e=MotionEvent.obtain(down,SystemClock.uptimeMillis(),action,target.width/2f,target.height/2f,0);target.dispatchTouchEvent(e);e.recycle()
        }}
        val held=SystemClock.uptimeMillis();event(MotionEvent.ACTION_DOWN,held)
        SystemClock.sleep(2600);assertTrue(CleanerPrefs.advancedMix(context))
        SystemClock.sleep(650);ins.waitForIdleSync()
        try {ins.runOnMainSync {
            assertFalse(CleanerPrefs.advancedMix(context))
            assertEquals(CleanerPrefs.SOURCE_OPTIMIZED,CleanerPrefs.sourcePreset(context))
            assertEquals(-100,CleanerPrefs.sourceMusicDb(context))
            assertFalse(all(page).any {it.tag=="advanced_mix_panel" && it.isShown})
            assertFalse(CleanerPrefs.isEnabled(context));assertFalse(CleanerService.isAlive)
        }} finally {event(MotionEvent.ACTION_UP,held)}
    }
    @Test fun captureAdvancedMixWhenRequested() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("captureAdvancedMix")=="true")
        for(language in listOf("en","ar")) isolated(language) {page->
            var dialog:Dialog?=null
            fun capture(name:String) {
                ins.waitForIdleSync();SystemClock.sleep(250)
                val bitmap=requireNotNull(ins.uiAutomation.takeScreenshot())
                File(context.getExternalFilesDir("advanced-mix-qa"),"$language-$name.png").outputStream().use {bitmap.compress(Bitmap.CompressFormat.PNG,100,it)}
                bitmap.recycle()
            }
            ins.runOnMainSync {dialog=AdvancedMixDialog.show(page){}}
            capture("dialog");enter(dialog!!,advancedCode());capture("mix")
        }
    }

}
