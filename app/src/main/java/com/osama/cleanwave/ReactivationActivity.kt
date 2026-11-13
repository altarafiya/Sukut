package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.database.ContentObserver
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout

/** A live, short recovery action. Ready replaces every setup/help control. */
class ReactivationActivity:Activity() {
    private lateinit var ui:MobileUi
    private lateinit var flow:LocalActivationFlow
    private lateinit var content:FrameLayout
    private var signature=""
    private var observing=false
    private val settings=object:ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange:Boolean){refresh()}
    }
    override fun attachBaseContext(base:Context){super.attachBaseContext(UiPrefs.wrapLocale(base))}
    override fun onCreate(state:Bundle?) {
        setTheme(if(UiPrefs.isDark(this))R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state);AppNavigation.configurePage(this)
        ui=MobileUi(this);ui.styleBars()
        flow=LocalActivationFlow(this,state,{refresh()},{refresh()})
        val root=ui.root(680)
        root.addView(LinearLayout(this).apply {
            gravity=Gravity.CENTER_VERTICAL
            addView(ui.iconButton(R.drawable.ic_back,R.string.back){close()}.apply {tag="recovery_back"},LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)))
            addView(ui.text(R.string.reactivate_now,24f,true).apply {isAccessibilityHeading=true},LinearLayout.LayoutParams(0,-2,1f).apply {marginStart=ui.dp(12)})
        },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(24)})
        content=FrameLayout(this).apply {tag="recovery_content"};root.addView(content)
        setContentView(ui.scroll(root));AppNavigation.handleBack(this){close()};refresh()
        if(state==null)activateFromIntent(intent)
    }
    private fun phone()=UiPrefs.activationMethod(this)==WorkerGuide.PHONE
    private fun activateFromIntent(intent:Intent) {
        if(intent.getBooleanExtra("activate_now",false) && phone() && DeveloperOptionsControl.readyForActivation(this) && !WorkerConnection.ready && !LocalActivationState.busy)flow.begin(LocalActivationService.ACTIVATE)
    }
    override fun onNewIntent(intent:Intent){super.onNewIntent(intent);setIntent(intent);signature="";refresh();activateFromIntent(intent)}
    private fun button(root:LinearLayout,id:Int,icon:Int=R.drawable.ic_worker,primary:Boolean=true,tag:String?=null,compact:Boolean=!primary,action:()->Unit) {
        root.addView(ui.button(id,icon,if(primary)MobileUi.Tone.FILLED else MobileUi.Tone.PLAIN,action).apply {this.tag=tag},LinearLayout.LayoutParams(if(compact)-2 else -1,-2).apply {topMargin=ui.dp(12);gravity=Gravity.CENTER_HORIZONTAL})
    }
    private fun refresh() {
        if(!::content.isInitialized || isFinishing || isDestroyed)return
        val ready=WorkerConnection.ready
        val developer=DeveloperOptionsControl.developerEnabled(this)
        val transport=if(phone())DeveloperOptionsControl.wirelessEnabled(this) else DeveloperOptionsControl.usbEnabled(this)
        val screen=RecoveryRoute.screen(ready,flow.notificationsBlocked,LocalActivationState.busy,developer,transport)
        val message=if(!ready && (flow.awaitingWorker || LocalActivationState.busy))LocalActivationState.message else 0
        val snapshot="$screen:$developer:$transport:${phone()}:${DeveloperOptionsControl.available(this)}:$message"
        if(snapshot==signature)return
        signature=snapshot
        val root=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;tag="recovery_${screen.name.lowercase()}"}
        when(screen) {
            RecoveryRoute.Screen.READY->{
                val green=ActivationStatus.readyColor(this,ui.cardColor)
                root.addView(ui.card(22).apply {
                    gravity=Gravity.CENTER_HORIZONTAL;tag="recovery_ready_card"
                    addView(ui.icon(R.drawable.ic_worker,green,48),LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)).apply {bottomMargin=ui.dp(16)})
                    addView(ui.text(R.string.worker_ready_short,23f,true,green).apply {gravity=Gravity.CENTER})
                    addView(ui.text(R.string.recovery_ready_detail,15f,color=ui.secondary).apply {gravity=Gravity.CENTER;setPadding(0,ui.dp(10),0,0)})
                })
                button(root,R.string.recovery_return,R.drawable.ic_back,tag="recovery_done",compact=true){close()}
            }
            RecoveryRoute.Screen.NOTIFICATIONS->{
                root.addView(ui.text(R.string.local_notifications_needed,16f,color=ui.secondary))
                root.addView(flow.notificationButton(ui).apply {visibility=View.VISIBLE})
            }
            RecoveryRoute.Screen.WORKING->{
                root.addView(ui.card(20).apply {
                    addView(ui.icon(R.drawable.ic_worker,ui.accent,32))
                    addView(ui.text(message.takeIf {it!=0}?:R.string.local_searching,17f,true).apply {tag="reactivation_message";setPadding(0,ui.dp(12),0,0);accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE})
                })
                button(root,R.string.cancel,0,false){PowerActivationRequest.cancel();flow.cancel()}
            }
            RecoveryRoute.Screen.DEVELOPER->{
                root.addView(ActivationStatus.card(this,0))
                if(DeveloperOptionsControl.available(this)) {
                    button(root,R.string.recovery_enable_debugging,R.drawable.ic_developer_tile,tag="recovery_enable") {enableAndActivate()}
                } else button(root,R.string.guide_about_phone,R.drawable.ic_phone){AudioTools.openAboutPhone(this)}
            }
            RecoveryRoute.Screen.TRANSPORT->{
                root.addView(ActivationStatus.card(this,if(phone())3 else 1))
                if(DeveloperOptionsControl.available(this))button(root,R.string.recovery_enable_debugging,R.drawable.ic_developer_tile,tag="recovery_enable"){enableAndActivate()}
                else button(root,R.string.guide_developer_options,R.drawable.ic_settings){openDebugging()}
            }
            RecoveryRoute.Screen.ACTIVATE->{
                root.addView(ActivationStatus.card(this,2))
                if(message!=0)root.addView(ui.text(message,14f,color=ui.warning).apply {tag="reactivation_message"})
                else root.addView(ui.text(R.string.worker_needed_detail,14f,color=ui.secondary).apply {tag="reactivation_message"})
                if(phone())button(root,R.string.local_activate_button,tag="reactivation_start"){flow.begin(LocalActivationService.ACTIVATE)}
                else root.addView(ui.card(16).apply {
                    addView(ui.icon(R.drawable.ic_setup_pc,ui.accent,28))
                    addView(ui.text(R.string.recovery_pc_route,16f,true,ui.accent).apply {setPadding(0,ui.dp(10),0,0)})
                },LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(12)})
            }
        }
        if(!ready && screen!=RecoveryRoute.Screen.NOTIFICATIONS)button(root,R.string.show_activation_guide,R.drawable.ic_activation_guide,false,"recovery_guide") {
            if(WorkerConnection.ready)refresh() else {
                flow.cancel();PowerActivationRequest.cancel()
                AppNavigation.openActivation(this,startStep=RecoveryRoute.guideStep(phone(),DeveloperOptionsControl.developerEnabled(this),if(phone())DeveloperOptionsControl.wirelessEnabled(this) else DeveloperOptionsControl.usbEnabled(this)))
            }
        }
        content.removeAllViews();content.addView(root,FrameLayout.LayoutParams(-1,-2));SukutTypography.apply(root)
    }
    private fun enableAndActivate(){if(DeveloperOptionsControl.changeFromGuide(this,true) && phone())flow.begin(LocalActivationService.ACTIVATE);refresh()}
    private fun openDebugging(){if(phone())AudioTools.openWirelessDebugging(this,704) else runCatching {startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS))}}
    private fun close(){flow.cancel();if(!WorkerConnection.ready)PowerActivationRequest.cancel();AppNavigation.closeSettings(this)}
    override fun onResume(){super.onResume();flow.resume();if(!observing){listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED,Settings.Global.ADB_ENABLED,"adb_wifi_enabled").forEach {contentResolver.registerContentObserver(Settings.Global.getUriFor(it),false,settings)};observing=true};refresh()}
    override fun onPause(){if(observing){contentResolver.unregisterContentObserver(settings);observing=false};flow.pause();super.onPause()}
    override fun onSaveInstanceState(state:Bundle){flow.save(state);super.onSaveInstanceState(state)}
    override fun onRequestPermissionsResult(code:Int,permissions:Array<out String>,results:IntArray){super.onRequestPermissionsResult(code,permissions,results);flow.permissionsResult(code,results)}
    override fun onDestroy(){if(observing)contentResolver.unregisterContentObserver(settings);flow.destroy();super.onDestroy()}
    @Deprecated("Legacy back dispatcher")override fun onBackPressed(){close()}
}
