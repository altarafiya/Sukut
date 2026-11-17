package com.osama.cleanwave

import android.content.Intent
import android.graphics.Bitmap
import android.os.Binder
import android.os.Bundle
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assume.assumeTrue
import org.junit.Assume.assumeFalse
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class RecoveryCaptureTest {
    @Test fun captureCompactRecoveryWhenRequested() {
        val ins=InstrumentationRegistry.getInstrumentation();val context=ins.targetContext
        assumeTrue(InstrumentationRegistry.getArguments().getString("captureGuidedActivation")=="true")
        assumeFalse(WorkerConnection.ready);assumeFalse(LocalActivationState.busy)
        val language=UiPrefs.language(context);val method=UiPrefs.activationMethod(context)
        try {for(locale in listOf("en","ar")) {
            UiPrefs.setLanguage(context,locale);UiPrefs.setActivationMethod(context,WorkerGuide.PHONE)
            val page=ins.startActivitySync(Intent(context,ReactivationActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            val binder=Binder()
            fun save(name:String) {
                ins.waitForIdleSync();SystemClock.sleep(300)
                val bitmap=requireNotNull(ins.uiAutomation.takeScreenshot())
                File(context.getExternalFilesDir("master-recovery-qa"),"$locale-$name.png").outputStream().use {bitmap.compress(Bitmap.CompressFormat.PNG,100,it)};bitmap.recycle()
            }
            try {
                save("next-action")
                ins.runOnMainSync {WorkerConnection.attach(Bundle().apply {putBinder("worker",binder);putInt("protocol",WorkerProtocol.VERSION);putInt("version",BuildConfig.VERSION_CODE);putInt("pid",12345)})}
                save("ready")
            } finally {ins.runOnMainSync {WorkerConnection.clear(binder);page.finish()};ins.waitForIdleSync()}
        }} finally {UiPrefs.setLanguage(context,language);UiPrefs.setActivationMethod(context,method)}
    }
}
