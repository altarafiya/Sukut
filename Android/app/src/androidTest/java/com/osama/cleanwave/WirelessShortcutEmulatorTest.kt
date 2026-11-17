package com.osama.cleanwave

import android.content.Intent
import android.os.Build
import android.os.SystemClock
import android.provider.Settings
import android.view.View
import android.view.ViewGroup
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in isolated emulator checks. Restores debugging settings even after a failure. */
@RunWith(AndroidJUnit4::class)
class WirelessShortcutEmulatorTest {
    private val ins=InstrumentationRegistry.getInstrumentation()
    private val app=ins.targetContext
    private fun emulatorOnly() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("allowDeveloperSettingsChanges")=="true")
        assumeTrue(Build.HARDWARE.contains("ranchu") || Build.HARDWARE.contains("goldfish"))
        assumeTrue(DeveloperOptionsControl.available(app))
    }
    private fun views(v:View):List<View> = buildList {add(v);if(v is ViewGroup)for(i in 0 until v.childCount)addAll(views(v.getChildAt(i)))}
    @Test fun tileActivationIntentUsesCompactRecoveryAndCancelsItsDiscovery() {
        emulatorOnly()
        val original=UiPrefs.activationMethod(app);UiPrefs.setActivationMethod(app,WorkerGuide.PHONE)
        var page:ReactivationActivity?=null
        try {
            page=ins.startActivitySync(Intent(app,ReactivationActivity::class.java).putExtra("activate_now",true).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)) as ReactivationActivity
            ins.waitForIdleSync();SystemClock.sleep(400)
            ins.runOnMainSync {
                assertFalse(views(page!!.window.decorView).any {it.tag=="guide_step_counter" || it.tag=="guide_phone"})
                assertTrue(views(page!!.window.decorView).any {it.tag in listOf("reactivation_start","recovery_enable","recovery_working")})
                page!!.finish()
            }
            ins.waitForIdleSync();SystemClock.sleep(400)
            // Explicit cancellation ends this bounded session.
            app.startService(Intent(app,LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
            SystemClock.sleep(400);assertFalse(LocalActivationState.busy)
        } finally {
            ins.runOnMainSync {page?.finish()}
            app.startService(Intent(app,LocalActivationService::class.java).setAction(LocalActivationService.CANCEL))
            UiPrefs.setActivationMethod(app,original)
        }
    }
    @Test fun shortcutRestoresOnlyItsChosenTransportAndTurnsAllDebuggingOff() {
        emulatorOnly()
        val keys=listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED,"adb_wifi_enabled",Settings.Global.ADB_ENABLED)
        val original=keys.associateWith {Settings.Global.getString(app.contentResolver,it)}
        val method=UiPrefs.activationMethod(app);val report=JSONObject();var failure:Throwable?=null
        try {
            assertTrue(DeveloperOptionsControl.setEnabled(app,false))
            UiPrefs.setActivationMethod(app,WorkerGuide.PHONE)
            assertTrue(DeveloperOptionsControl.setEnabled(app,true))
            assertTrue(DeveloperOptionsControl.developerEnabled(app));assertTrue(DeveloperOptionsControl.wirelessEnabled(app));assertFalse(DeveloperOptionsControl.usbEnabled(app))
            report.put("wireless_enable_request_without_usb",true)
            assertTrue(DeveloperOptionsControl.setEnabled(app,false));assertTrue(DeveloperOptionsControl.allOff(app))
            report.put("all_debugging_off",true)
            UiPrefs.setActivationMethod(app,WorkerGuide.WINDOWS)
            assertTrue(DeveloperOptionsControl.setEnabled(app,true));assertTrue(DeveloperOptionsControl.readyForUsb(app));assertFalse(DeveloperOptionsControl.wirelessEnabled(app))
            report.put("usb_enable_without_wireless",true)
        } catch(problem:Throwable){failure=problem;report.put("failure",problem.toString())}
        finally {
            for(key in keys)Settings.Global.putString(app.contentResolver,key,original[key])
            UiPrefs.setActivationMethod(app,method)
            report.put("original_settings_restored",keys.all {Settings.Global.getString(app.contentResolver,it)==original[it]})
            report.put("passed",failure==null)
            File(app.getExternalFilesDir("wireless-shortcut-qa"),"result.json").writeText(report.toString(2))
        }
        failure?.let {throw it}
    }
}
