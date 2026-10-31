package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/** One activation destination; Settings opens only after its reply notification is posted. */
class LocalActivationActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var status: TextView
    private lateinit var notificationAction: View
    private lateinit var flow: LocalActivationFlow
    private var method=WorkerGuide.PHONE
    override fun attachBaseContext(base:Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state:Bundle?) {
        setTheme(if(UiPrefs.isDark(this))R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state); AppNavigation.configurePage(this)
        ui=MobileUi(this); ui.styleBars()
        method=state?.getInt("method")?:intent.getIntExtra("method",UiPrefs.activationMethod(this))
        if(Build.VERSION.SDK_INT<30)method=WorkerGuide.WINDOWS
        val root=ui.root(680)
        root.addView(LinearLayout(this).apply {
            gravity=android.view.Gravity.CENTER_VERTICAL
            addView(ui.iconButton(R.drawable.ic_back,R.string.back) { cancelAndClose() },LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)))
            addView(ui.text(R.string.worker,24f,true).apply { isAccessibilityHeading=true },LinearLayout.LayoutParams(0,-2,1f).apply { marginStart=ui.dp(14) })
        })
        status=ui.text("",14f,true,ui.accent).apply { setPadding(0,ui.dp(14),0,ui.dp(14)) }
        root.addView(status)
        flow=LocalActivationFlow(this,state,{ refresh() },{ goHome() })
        notificationAction=flow.notificationButton(ui)
        root.addView(notificationAction)
        root.addView(ui.text(R.string.guide_choose_method,14f,color=ui.secondary).apply { setPadding(0,ui.dp(4),0,ui.dp(12)) })
        root.addView(WorkerGuide.create(this,method,{ goHome() },{ flow.begin(LocalActivationService.PAIR,true) }) { selected ->
            method=selected
            if(selected==WorkerGuide.WINDOWS)flow.cancel(keepReturn=true)
            else if(flow.awaitingWorker && intent.getStringExtra("activation_action")==LocalActivationService.ACTIVATE && !WorkerConnection.ready)
                flow.begin(LocalActivationService.ACTIVATE)
        })
        setContentView(ui.scroll(root)); SukutTypography.apply(root)
        AppNavigation.handleBack(this) { cancelAndClose() }
        if(state==null)intent.getStringExtra("activation_action")?.let { action ->
            if(action==LocalActivationService.PAIR)flow.begin(action,true)
            else if(action==LocalActivationService.ACTIVATE && method==WorkerGuide.PHONE)flow.begin(action)
        }
    }
    private fun refresh() {
        if(!::notificationAction.isInitialized)return
        notificationAction.visibility=if(flow.notificationsBlocked)View.VISIBLE else View.GONE
        status.setText(if(flow.notificationsBlocked)R.string.local_notifications_needed else if(WorkerConnection.ready)R.string.worker_ready_short
            else LocalActivationState.message.takeIf { it!=0 }?:R.string.worker_not_running)
    }
    private fun goHome() {
        if(intent.getBooleanExtra("return_to_setup",false)) { finish(); return }
        startActivity(Intent(this,MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP)); finish()
    }
    private fun cancelAndClose() { PowerActivationRequest.cancel(); flow.cancel(); finish() }
    override fun onRequestPermissionsResult(code:Int,permissions:Array<out String>,results:IntArray) {
        super.onRequestPermissionsResult(code,permissions,results); flow.permissionsResult(code,results)
    }
    override fun onSaveInstanceState(state:Bundle) { flow.save(state); state.putInt("method",method); super.onSaveInstanceState(state) }
    override fun onResume() { super.onResume(); flow.resume() }
    override fun onPause() { flow.pause(); super.onPause() }
    override fun onDestroy() { flow.destroy(); super.onDestroy() }
    @Deprecated("Legacy back dispatcher") override fun onBackPressed() { cancelAndClose() }
}
