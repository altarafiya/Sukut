package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.content.ComponentName
import android.media.audiofx.AudioEffect
import android.net.Uri
import android.provider.Settings
import android.widget.Toast

internal object AudioTools {
    fun shizukuInstalled(activity: Activity): Boolean = runCatching { activity.packageManager.getPackageInfo("moe.shizuku.privileged.api", 0) }.isSuccess
    fun openVolume(activity: Activity) = open(activity, listOf(Intent(Settings.Panel.ACTION_VOLUME), Intent(Settings.ACTION_SOUND_SETTINGS)))
    fun openEffects(activity: Activity) {
        // MusicFX's Compatibility.Redirector can accept the session-less intent
        // and then silently close. Use Xiaomi's exported, installed effects page.
        val xiaomi = Intent("miui.intent.action.HEADSET_SETTINGS").setPackage("com.miui.misound")
        val target = xiaomi.resolveActivityInfo(activity.packageManager, 0)
        if (target?.exported == true && target.enabled && runCatching { activity.startActivity(xiaomi) }.isSuccess) return
        open(activity, listOf(Intent(Settings.ACTION_SOUND_SETTINGS)))
    }
    fun openShizuku(activity: Activity) {
        val intent = activity.packageManager.getLaunchIntentForPackage("moe.shizuku.privileged.api")
        if (intent != null) open(activity, listOf(intent))
        else openShizukuStore(activity)
    }
    fun openShizukuStore(activity: Activity) = open(activity, listOf(
        Intent(Intent.ACTION_VIEW, Uri.parse("market://details?id=moe.shizuku.privileged.api")),
        Intent(Intent.ACTION_VIEW, Uri.parse("https://play.google.com/store/apps/details?id=moe.shizuku.privileged.api"))))
    fun openAboutPhone(activity: Activity) = open(activity, listOf(
        Intent(Settings.ACTION_DEVICE_INFO_SETTINGS), Intent(Settings.ACTION_SETTINGS)))
    fun website(activity: Activity, url: String) = open(activity, listOf(Intent(Intent.ACTION_VIEW, Uri.parse(url))))
    fun openWirelessDebugging(activity: Activity) = open(activity, listOf(
        Intent("android.settings.WIRELESS_DEBUGGING_SETTINGS"), Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS)))
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
