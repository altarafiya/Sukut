package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/**
 * The computer method's twin of the phone method's spark screen. Sukut on Windows (and
 * later Linux) opens this over USB when Activate is pressed (`am start`), so the phone
 * shows the same grey س throwing sparks while the computer starts the audio worker,
 * then catches fire when the worker connects and returns Home by itself.
 *
 * Only callers holding WRITE_SECURE_SETTINGS (the ADB shell) can open it: other apps
 * cannot. It changes nothing; it only watches [WorkerConnection].
 */
class ComputerActivationActivity:Activity() {
    private lateinit var ui:MobileUi
    private lateinit var spark:ActivationSparkView
    private lateinit var title:TextView
    private lateinit var step:TextView
    private lateinit var hint:TextView
    private lateinit var cancel:View
    private var closing=false
    private val worker=WorkerConnection.OnChanged { runOnUiThread { update() } }
    /** Long activations (first install, slow phones): say what to check, keep the sparks going. */
    private val slow=Runnable { if(!WorkerConnection.ready && !isFinishing){step.setText(R.string.pc_auto_slow);SukutTypography.apply(step)} }

    override fun attachBaseContext(base:Context){super.attachBaseContext(UiPrefs.wrapLocale(base))}
    override fun onCreate(state:Bundle?) {
        setTheme(if(UiPrefs.isDark(this))R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state);AppNavigation.configurePage(this)
        ui=MobileUi(this);ui.styleBars()
        val root=ui.root(680)
        val page=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER_HORIZONTAL;tag="computer_auto"}
        spark=ActivationSparkView(this,ui.secondary).apply {tag="activation_spark"}
        page.addView(spark,LinearLayout.LayoutParams(-1,ui.dp(260)).apply {topMargin=ui.dp(56)})
        title=ui.text(R.string.auto_title,24f,true).apply {gravity=Gravity.CENTER;tag="auto_title";accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        step=ui.text(R.string.pc_auto_step,15f,color=ui.secondary).apply {gravity=Gravity.CENTER;tag="auto_step";setPadding(0,ui.dp(8),0,0);accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        hint=ui.text(R.string.pc_auto_hint,13f,color=ui.secondary).apply {gravity=Gravity.CENTER;alpha=.8f;setPadding(0,ui.dp(4),0,0)}
        page.addView(title,LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(4)})
        page.addView(step,LinearLayout.LayoutParams(-1,-2));page.addView(hint,LinearLayout.LayoutParams(-1,-2))
        cancel=ui.button(R.string.pc_auto_close,0,MobileUi.Tone.PLAIN){close()}.apply {tag="auto_cancel"}
        page.addView(cancel,LinearLayout.LayoutParams(-2,-2).apply {topMargin=ui.dp(28);gravity=Gravity.CENTER_HORIZONTAL})
        root.addView(page)
        setContentView(ui.scroll(root));AppNavigation.handleBack(this){close()}
        SukutTypography.apply(page)
        if(ui.motion)StartupMotion.hero(page)
    }
    private fun update() {
        if(isFinishing || isDestroyed || closing || !WorkerConnection.ready)return
        closing=true;step.removeCallbacks(slow)
        spark.ready()
        title.setText(R.string.auto_ready);step.setText(R.string.pc_auto_ready_detail)
        title.setTextColor(ActivationStatus.readyColor(this,ui.background))
        hint.visibility=View.INVISIBLE;cancel.visibility=View.INVISIBLE
        SukutTypography.apply(title);SukutTypography.apply(step)
        step.postDelayed({if(!isFinishing)close()},if(ui.motion)2200 else 900)
    }
    // A fresh request from the computer (Activate pressed again) restarts the wait, not the screen.
    override fun onNewIntent(intent:Intent){super.onNewIntent(intent);setIntent(intent)}
    override fun onResume() {
        super.onResume()
        WorkerConnection.addListener(worker)
        step.removeCallbacks(slow);step.postDelayed(slow,20_000)
    }
    override fun onPause(){WorkerConnection.removeListener(worker);step.removeCallbacks(slow);super.onPause()}
    private fun close(){step.removeCallbacks(slow);AppNavigation.closeSettings(this)}
    @Deprecated("Legacy back dispatcher")override fun onBackPressed(){close()}
}
