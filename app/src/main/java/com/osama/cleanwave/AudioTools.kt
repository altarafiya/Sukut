package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.content.ComponentName
import android.media.audiofx.AudioEffect
import android.net.Uri
import android.provider.Settings
import android.widget.Toast

internal object AudioTools {
    fun openVolume(activity: Activity) = open(activity, listOf(Intent(Settings.Panel.ACTION_VOLUME), Intent(Settings.ACTION_SOUND_SETTINGS)))
    fun openEffects(activity: Activity) {
        // MusicFX's Compatibility.Redirector can accept the session-less intent
        // and then silently close. Use Xiaomi's exported, installed effects page.
        val xiaomi = Intent("miui.intent.action.HEADSET_SETTINGS").setPackage("com.miui.misound")
        val target = xiaomi.resolveActivityInfo(activity.packageManager, 0)
        if (target?.exported == true && target.enabled && runCatching { activity.startActivity(xiaomi) }.isSuccess) return
        open(activity, listOf(Intent(Settings.ACTION_SOUND_SETTINGS)))
    }
    fun openAboutPhone(activity: Activity) = open(activity, listOf(
        Intent(Settings.ACTION_DEVICE_INFO_SETTINGS), Intent(Settings.ACTION_SETTINGS)))
    fun website(activity: Activity, url: String) = open(activity, listOf(Intent(Intent.ACTION_VIEW, Uri.parse(url))))
    fun support(activity:Activity) {
        if(runCatching { activity.startActivity(Intent(Intent.ACTION_SENDTO,Uri.parse("mailto:"+ReleaseLinks.SUPPORT_EMAIL))) }.isSuccess)return
        activity.getSystemService(android.content.ClipboardManager::class.java)
            .setPrimaryClip(android.content.ClipData.newPlainText("Sukut support",ReleaseLinks.SUPPORT_EMAIL))
        Toast.makeText(activity,R.string.email_copied,Toast.LENGTH_SHORT).show()
    }
    @Suppress("DEPRECATION")
    fun openWirelessDebugging(activity: Activity, requestCode:Int?=null) {
        val targets=listOf(
            Intent("android.settings.WIRELESS_DEBUGGING_SETTINGS"),
            Intent().setComponent(ComponentName("com.android.settings","com.android.settings.SubSettings"))
                .putExtra(":settings:show_fragment","com.android.settings.development.WirelessDebuggingFragment"),
            Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS).putExtra(":settings:fragment_args_key","toggle_adb_wireless"))
        for(target in targets) {
            if(runCatching { if(requestCode==null)activity.startActivity(target) else activity.startActivityForResult(target,requestCode) }.isSuccess)return
        }
        Toast.makeText(activity,R.string.cannot_open_settings,Toast.LENGTH_LONG).show()
    }
    private fun open(activity: Activity, intents: List<Intent>) {
        for (intent in intents) {
            if (runCatching { activity.startActivity(intent) }.isSuccess) return
        }
        Toast.makeText(activity, R.string.cannot_open_settings, Toast.LENGTH_LONG).show()
    }
    fun effectsSummary(activity: Activity): String {
        val dolby = runCatching { AudioEffect.queryEffects()?.any {
            it.name.contains("dolby", true) || it.implementor.contains("dolby", true)
        } == true }.getOrDefault(false)
        return activity.getString(if (dolby) R.string.dolby_available else R.string.effects_system_managed)
    }
}
