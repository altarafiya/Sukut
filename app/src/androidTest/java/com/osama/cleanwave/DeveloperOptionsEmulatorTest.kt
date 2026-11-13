package com.osama.cleanwave

import android.os.Binder
import android.os.Build
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.os.SystemClock
import android.provider.Settings
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeFalse
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Explicit emulator-only system-setting check; restores every changed setting. */
@RunWith(AndroidJUnit4::class)
class DeveloperOptionsEmulatorTest {
    @Test fun shellAcknowledgementAndRealSettingsWrites() {
        val ins=InstrumentationRegistry.getInstrumentation();val app=ins.targetContext
        assumeTrue(InstrumentationRegistry.getArguments().getString("allowDeveloperSettingsChanges")=="true")
        assumeTrue(Build.HARDWARE.contains("ranchu") || Build.HARDWARE.contains("goldfish"))
        assumeTrue(DeveloperOptionsControl.available(app));assumeFalse(WorkerConnection.ready)
        val keys=listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED,"adb_wifi_enabled",Settings.Global.ADB_ENABLED)
        val original=keys.associateWith {Settings.Global.getString(app.contentResolver,it)}
        val method=UiPrefs.activationMethod(app)
        val binder=Binder();val report=JSONObject();var failure:Throwable?=null
        try {
            ins.runOnMainSync {WorkerConnection.attach(Bundle().apply {
                putInt("protocol",WorkerProtocol.VERSION);putInt("version",BuildConfig.VERSION_CODE)
                putInt("pid",android.os.Process.myPid());putBinder("worker",binder)
            })}
            val acknowledgement=ParcelFileDescriptor.AutoCloseInputStream(ins.uiAutomation.executeShellCommand(
                "content call --user 0 --uri content://com.altarafiyah.sukut.worker --method computer-setup"
            )).bufferedReader().use {it.readText()}
            assertTrue(acknowledgement.contains("state=COMPUTER_SETUP_READY"));report.put("shell_acknowledgement",true)
            assertTrue(DeveloperOptionsControl.setEnabled(app,true));assertTrue(DeveloperOptionsControl.readyForUsb(app))
            report.put("enable_developer_and_usb",true)
            assertTrue(DeveloperOptionsControl.setEnabled(app,false));assertTrue(DeveloperOptionsControl.allOff(app))
            report.put("disable_developer_usb_wireless",true)
            assertTrue(WorkerConnection.ready);report.put("local_binder_connection_retained",true)
            // Let Android process the daemon stop; the instrumentation process is independent.
            SystemClock.sleep(500)
        } catch(problem:Throwable) {failure=problem;report.put("failure",problem.toString())}
        finally {
            for(key in keys)Settings.Global.putString(app.contentResolver,key,original[key])
            ins.runOnMainSync {WorkerConnection.clear(binder)}
            DeveloperOptionsControl.dismissCleanup(app);UiPrefs.setActivationMethod(app,method)
            report.put("original_settings_restored",keys.all {Settings.Global.getString(app.contentResolver,it)==original[it]})
            report.put("passed",failure==null)
            File(app.getExternalFilesDir("developer-options-qa"),"result.json").writeText(report.toString(2))
        }
        failure?.let {throw it}
    }
}
