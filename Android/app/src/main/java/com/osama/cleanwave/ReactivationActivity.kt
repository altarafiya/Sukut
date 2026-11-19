package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.database.ContentObserver
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView

/**
 * A live, short recovery action. On the phone method Sukut does every step by itself
 * (Developer options, Wireless debugging, the worker) behind one animated screen; the
 * step-by-step screens appear only when a step fails, saying which one and what to do.
 * Ready replaces every setup/help control.
 */
class ReactivationActivity:Activity() {
    private lateinit var ui:MobileUi
    private lateinit var flow:LocalActivationFlow
    private lateinit var content:FrameLayout
    private lateinit var headerTitle:TextView
    private var signature=""
    private var observing=false
    private val settings=object:ContentObserver(Handler(Looper.getMainLooper())) {
        override fun onChange(selfChange:Boolean){refresh()}
    }

    private enum class Auto { OFF, RUNNING, FAILED }
    private enum class Failure { NONE, WIFI, DEBUGGING, WORKER }
    private var auto=Auto.OFF
    private var failure=Failure.NONE
    private var failureMessage=0
    /** The activation service was asked to start for this attempt. */
    private var started=false
    private var closing=false
    /** Wireless debugging was just switched on and is given a moment to start listening. */
    private var warming=false
    /** Silent retries left before a connection failure is shown. */
    private var retries=0
    private var autoPage:LinearLayout?=null
    private var spark:ActivationSparkView?=null
    private var autoTitle:TextView?=null
    private var autoStep:TextView?=null
    private var autoHint:TextView?=null
    private var autoCancel:View?=null
    private val steps=Runnable { runSteps() }
    private val connect=Runnable { if(auto==Auto.RUNNING && !isFinishing && !isDestroyed){warming=false;started=true;flow.begin(LocalActivationService.ACTIVATE);refresh()} }
    private val retryAfterWifi=Runnable { if(auto==Auto.FAILED && failure==Failure.WIFI && !isFinishing)startAuto() }
    private var wifiWatching=false
    private val wifi=object:ConnectivityManager.NetworkCallback() {
        override fun onAvailable(network:Network){runOnUiThread {wifiChanged(true)}}
        override fun onLost(network:Network){runOnUiThread {wifiChanged(false)}}
    }

    override fun attachBaseContext(base:Context){super.attachBaseContext(UiPrefs.wrapLocale(base))}
    override fun onCreate(state:Bundle?) {
        setTheme(if(UiPrefs.isDark(this))R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state);AppNavigation.configurePage(this)
        ui=MobileUi(this);ui.styleBars()
        state?.let {
            auto=Auto.entries[it.getInt("auto",0)];failure=Failure.entries[it.getInt("auto_failure",0)]
            failureMessage=it.getInt("auto_message",0);started=it.getBoolean("auto_started",false)
        }
        flow=LocalActivationFlow(this,state,{refresh()},{refresh()})
        val root=ui.root(680)
        headerTitle=ui.text(R.string.reactivate_now,24f,true).apply {isAccessibilityHeading=true}
        root.addView(LinearLayout(this).apply {
            gravity=Gravity.CENTER_VERTICAL
            addView(ui.iconButton(R.drawable.ic_back,R.string.back){close()}.apply {tag="recovery_back"},LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)))
            addView(headerTitle,LinearLayout.LayoutParams(0,-2,1f).apply {marginStart=ui.dp(12)})
        },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(24)})
        content=FrameLayout(this).apply {tag="recovery_content"};root.addView(content)
        setContentView(ui.scroll(root));AppNavigation.handleBack(this){close()}
        if(auto==Auto.RUNNING && !started)content.post(steps)
        refresh()
        if(state==null)activateFromIntent(intent)
    }
    private fun phone()=UiPrefs.activationMethod(this)==WorkerGuide.PHONE
    private fun activateFromIntent(intent:Intent) {
        if(intent.getBooleanExtra("activate_now",false) && phone() && !WorkerConnection.ready)startAuto()
    }
    override fun onNewIntent(intent:Intent){super.onNewIntent(intent);setIntent(intent);signature="";refresh();activateFromIntent(intent)}

    /** One tap: Sukut tries every step itself while the spark plays. */
    private fun startAuto() {
        content.removeCallbacks(steps);content.removeCallbacks(connect);content.removeCallbacks(retryAfterWifi)
        if(!phone() || WorkerConnection.ready){refresh();return}
        failure=Failure.NONE;failureMessage=0;started=false;warming=false;retries=1
        if(LocalActivationState.busy){auto=Auto.RUNNING;started=true;refresh();return}
        if(!DeveloperOptionsControl.wifiConnected(this)){fail(Failure.WIFI);return}
        // Without the computer-granted permission Sukut cannot switch debugging on: show the usual steps.
        if(!DeveloperOptionsControl.readyForActivation(this) && !DeveloperOptionsControl.available(this)){auto=Auto.OFF;signature="";refresh();return}
        auto=Auto.RUNNING;refresh()
        // Let the spark catch first, so the switch to the animation never looks like a flash.
        content.postDelayed(steps,if(ui.motion)500 else 0)
    }
    private fun runSteps() {
        if(auto!=Auto.RUNNING || isFinishing || isDestroyed)return
        val switching=!DeveloperOptionsControl.readyForActivation(this)
        if(switching && !DeveloperOptionsControl.setEnabled(this,true)) {
            fail(if(!DeveloperOptionsControl.wifiConnected(this))Failure.WIFI else Failure.DEBUGGING);return
        }
        // Wireless debugging needs a moment after it is switched on before it accepts a connection.
        if(switching){warming=true;refresh();content.postDelayed(connect,2500)} else connect.run()
    }
    private fun fail(kind:Failure,message:Int=0) {
        content.removeCallbacks(steps);content.removeCallbacks(connect);warming=false
        auto=Auto.FAILED;failure=kind;failureMessage=message;started=false;signature="";refresh()
    }
    private fun wifiChanged(available:Boolean) {
        content.removeCallbacks(retryAfterWifi)
        // Wi-Fi is back after a Wi-Fi failure: carry on by itself once Wireless debugging can listen again.
        if(available && auto==Auto.FAILED && failure==Failure.WIFI)content.postDelayed(retryAfterWifi,2500)
        refresh()
    }

    private fun button(root:LinearLayout,id:Int,icon:Int=R.drawable.ic_worker,primary:Boolean=true,tag:String?=null,compact:Boolean=!primary,action:()->Unit) {
        root.addView(ui.button(id,icon,if(primary)MobileUi.Tone.FILLED else MobileUi.Tone.PLAIN,action).apply {this.tag=tag},LinearLayout.LayoutParams(if(compact)-2 else -1,-2).apply {topMargin=ui.dp(12);gravity=Gravity.CENTER_HORIZONTAL})
    }
    private fun refresh() {
        if(!::content.isInitialized || isFinishing || isDestroyed)return
        val ready=WorkerConnection.ready
        // A background attempt (Debugging tile, Wi-Fi returning) shows the same animation.
        if(auto==Auto.OFF && LocalActivationState.busy && phone() && !ready){auto=Auto.RUNNING;started=true}
        if(auto==Auto.RUNNING) {
            when {
                ready->{showAuto(true);return}
                flow.notificationsBlocked->Unit // Android must allow notifications first: the usual screen below.
                started && !LocalActivationState.busy->{
                    val message=LocalActivationState.message
                    if(message==R.string.local_cancelled){auto=Auto.OFF;signature=""}
                    else if(retries>0 && (message==R.string.local_activate_failed || message==R.string.local_timeout) && DeveloperOptionsControl.wifiConnected(this)) {
                        // One quiet retry before showing anything: Wireless debugging is often still starting.
                        retries--;started=false;content.postDelayed(connect,2000);showAuto(false);return
                    }
                    else {fail(if(message==R.string.local_no_wifi)Failure.WIFI else Failure.WORKER,message);return}
                }
                else->{showAuto(false);return}
            }
        }
        if(ready){auto=Auto.OFF;failure=Failure.NONE}
        // The user turned debugging on in Settings after Sukut could not: carry on by itself.
        else if(auto==Auto.FAILED && failure==Failure.DEBUGGING && DeveloperOptionsControl.readyForActivation(this)){startAuto();return}
        headerTitle.visibility=View.VISIBLE
        val developer=DeveloperOptionsControl.developerEnabled(this)
        val transport=if(phone())DeveloperOptionsControl.wirelessEnabled(this) else DeveloperOptionsControl.usbEnabled(this)
        val wifiMissing=phone() && !DeveloperOptionsControl.wifiConnected(this)
        val screen=RecoveryRoute.screen(ready,flow.notificationsBlocked,LocalActivationState.busy,developer,transport)
        val message=if(!ready && (flow.awaitingWorker || LocalActivationState.busy))LocalActivationState.message else 0
        val snapshot="$screen:$developer:$transport:${phone()}:${DeveloperOptionsControl.available(this)}:$message:$auto:$failure:$failureMessage:$wifiMissing"
        if(snapshot==signature)return
        signature=snapshot
        val root=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;tag="recovery_${screen.name.lowercase()}"}
        if(!ready && screen!=RecoveryRoute.Screen.NOTIFICATIONS && (failure==Failure.WIFI || (wifiMissing && screen!=RecoveryRoute.Screen.WORKING))) {
            wifiPage(root)
        } else {
            if(auto==Auto.FAILED && !ready)when(failure) {
                Failure.DEBUGGING->failureCard(root,R.string.fail_debugging_title,R.string.fail_debugging_body)
                Failure.WORKER->failureCard(root,R.string.fail_worker_title,failureMessage.takeIf {it!=0}?:R.string.local_activate_failed)
                else->Unit
            }
            routePage(root,screen,message)
        }
        if(!ready && screen!=RecoveryRoute.Screen.NOTIFICATIONS)button(root,R.string.show_activation_guide,R.drawable.ic_activation_guide,false,"recovery_guide") {
            if(WorkerConnection.ready)refresh() else {
                flow.cancel();PowerActivationRequest.cancel()
                // The full guide starts at choosing computer or phone, then skips the steps already done.
                AppNavigation.openActivation(this,startStep=-1)
            }
        }
        content.removeAllViews();content.addView(root,FrameLayout.LayoutParams(-1,-2));SukutTypography.apply(root)
    }
    private fun routePage(root:LinearLayout,screen:RecoveryRoute.Screen,message:Int) {
        val failed=auto==Auto.FAILED
        when(screen) {
            RecoveryRoute.Screen.READY->{
                val green=ActivationStatus.readyColor(this,ui.cardColor)
                root.addView(ui.card(22).apply {
                    gravity=Gravity.CENTER_HORIZONTAL;tag="recovery_ready_card"
                    addView(ui.icon(R.drawable.ic_worker,green,48),LinearLayout.LayoutParams(ui.dp(48),ui.dp(48)).apply {bottomMargin=ui.dp(16)})
                    addView(ui.text(R.string.worker_ready_short,23f,true,green).apply {gravity=Gravity.CENTER})
                    addView(ui.text(R.string.recovery_ready_detail,15f,color=ui.secondary).apply {gravity=Gravity.CENTER;setPadding(0,ui.dp(10),0,0)})
                })
                // Done: show it briefly, then return to Sukut by itself.
                content.postDelayed({ if(!isFinishing && WorkerConnection.ready) close() },1200)
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
                if(failed && failure==Failure.DEBUGGING) {
                    button(root,R.string.guide_about_phone,R.drawable.ic_phone){AudioTools.openAboutPhone(this)}
                    button(root,R.string.try_again,0,false,"recovery_retry"){startAuto()}
                } else if(DeveloperOptionsControl.available(this)) {
                    button(root,if(failed)R.string.try_again else R.string.recovery_enable_debugging,R.drawable.ic_developer_tile,tag="recovery_enable") {enableAndActivate()}
                } else button(root,R.string.guide_about_phone,R.drawable.ic_phone){AudioTools.openAboutPhone(this)}
            }
            RecoveryRoute.Screen.TRANSPORT->{
                root.addView(ActivationStatus.card(this,if(phone())3 else 1))
                if(failed && failure==Failure.DEBUGGING) {
                    button(root,R.string.guide_developer_options,R.drawable.ic_settings){openDebugging()}
                    button(root,R.string.try_again,0,false,"recovery_retry"){startAuto()}
                } else if(DeveloperOptionsControl.available(this))button(root,if(failed)R.string.try_again else R.string.recovery_enable_debugging,R.drawable.ic_developer_tile,tag="recovery_enable"){enableAndActivate()}
                else button(root,R.string.guide_developer_options,R.drawable.ic_settings){openDebugging()}
            }
            RecoveryRoute.Screen.ACTIVATE->{
                root.addView(ActivationStatus.card(this,2))
                if(!failed) {
                    if(message!=0)root.addView(ui.text(message,14f,color=ui.warning).apply {tag="reactivation_message"})
                    else root.addView(ui.text(R.string.worker_needed_detail,14f,color=ui.secondary).apply {tag="reactivation_message"})
                }
                if(phone()) {
                    button(root,if(failed)R.string.try_again else R.string.local_activate_button,tag="reactivation_start"){startAuto()}
                    root.addView(ui.text(R.string.phone_wifi_note,13f,color=ui.secondary).apply {setPadding(0,ui.dp(14),0,0)})
                }
                else root.addView(ui.card(16).apply {
                    addView(ui.icon(R.drawable.ic_setup_pc,ui.accent,28))
                    addView(ui.text(R.string.recovery_pc_route,16f,true,ui.accent).apply {setPadding(0,ui.dp(10),0,0)})
                },LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(12)})
            }
        }
    }
    /** What went wrong, in one short card above the usual steps. */
    private fun failureCard(root:LinearLayout,title:Int,body:Int) {
        root.addView(ui.card(18).apply {
            tag="recovery_failure";background=ui.rounded(ThemeColors.mix(ui.cardColor,ui.warning,.10f),22)
            addView(ui.text(title,18f,true,ui.warning).apply {isAccessibilityHeading=true;accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE})
            addView(ui.text(body,14f,color=ui.primary).apply {tag="recovery_failure_detail";setPadding(0,ui.dp(8),0,0)})
        },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(12)})
    }
    /** Phone activation without Wi-Fi: the one thing to fix, said plainly. */
    private fun wifiPage(root:LinearLayout) {
        root.tag="recovery_wifi"
        root.addView(ui.card(22).apply {
            gravity=Gravity.CENTER_HORIZONTAL;tag="recovery_wifi_card"
            addView(ui.icon(R.drawable.ic_setup_wireless,ui.warning,44),LinearLayout.LayoutParams(ui.dp(44),ui.dp(44)).apply {bottomMargin=ui.dp(14)})
            addView(ui.text(R.string.fail_wifi_title,22f,true).apply {gravity=Gravity.CENTER;isAccessibilityHeading=true;accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE})
            addView(ui.text(R.string.fail_wifi_body,15f,color=ui.secondary).apply {gravity=Gravity.CENTER;tag="recovery_wifi_detail";setPadding(0,ui.dp(10),0,0)})
        })
        button(root,R.string.open_wifi,R.drawable.ic_setup_wireless,tag="recovery_open_wifi"){AudioTools.openWifi(this)}
        button(root,R.string.try_again,0,false,"recovery_retry"){startAuto()}
        root.addView(ui.text(R.string.fail_wifi_auto,13f,color=ui.secondary).apply {gravity=Gravity.CENTER;setPadding(0,ui.dp(14),0,0)})
    }
    /** The spark page stays the same view through every step, so the animation never restarts. */
    private fun showAuto(ready:Boolean) {
        val page=autoPage?:buildAutoPage()
        if(page.parent!==content){content.removeAllViews();content.addView(page,FrameLayout.LayoutParams(-1,-2))}
        signature="";headerTitle.visibility=View.INVISIBLE
        if(ready) {
            spark?.ready()
            setTextIfChanged(autoTitle,R.string.auto_ready);setTextIfChanged(autoStep,R.string.auto_ready_detail)
            autoTitle?.setTextColor(ActivationStatus.readyColor(this,ui.background))
            autoHint?.visibility=View.INVISIBLE;autoCancel?.visibility=View.INVISIBLE
            if(!closing){closing=true;content.postDelayed({if(!isFinishing)close()},if(ui.motion)1800 else 700)}
            return
        }
        val message=LocalActivationState.message
        setTextIfChanged(autoStep,when {
            !started->if(warming || !DeveloperOptionsControl.readyForActivation(this))R.string.auto_step_debugging else R.string.auto_step_connecting
            message==R.string.local_activating || message==R.string.local_paired->R.string.auto_step_worker
            else->R.string.auto_step_connecting
        })
    }
    private fun setTextIfChanged(view:TextView?,id:Int){val text=getString(id);if(view!=null && view.text.toString()!=text){view.text=text;SukutTypography.apply(view)}}
    private fun buildAutoPage():LinearLayout {
        val page=LinearLayout(this).apply {orientation=LinearLayout.VERTICAL;gravity=Gravity.CENTER_HORIZONTAL;tag="recovery_auto"}
        spark=ActivationSparkView(this,ui.secondary).apply {tag="activation_spark"}
        page.addView(spark,LinearLayout.LayoutParams(-1,ui.dp(260)).apply {topMargin=ui.dp(8)})
        autoTitle=ui.text(R.string.auto_title,24f,true).apply {gravity=Gravity.CENTER;tag="auto_title";accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        autoStep=ui.text(R.string.auto_step_connecting,15f,color=ui.secondary).apply {gravity=Gravity.CENTER;tag="auto_step";setPadding(0,ui.dp(8),0,0);accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE}
        autoHint=ui.text(R.string.auto_hint,13f,color=ui.secondary).apply {gravity=Gravity.CENTER;alpha=.8f;setPadding(0,ui.dp(4),0,0)}
        page.addView(autoTitle,LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(4)})
        page.addView(autoStep,LinearLayout.LayoutParams(-1,-2))
        page.addView(autoHint,LinearLayout.LayoutParams(-1,-2))
        autoCancel=ui.button(R.string.cancel,0,MobileUi.Tone.PLAIN){PowerActivationRequest.cancel();content.removeCallbacks(steps);content.removeCallbacks(connect);flow.cancel();close()}.apply {tag="auto_cancel"}
        page.addView(autoCancel,LinearLayout.LayoutParams(-2,-2).apply {topMargin=ui.dp(28);gravity=Gravity.CENTER_HORIZONTAL})
        SukutTypography.apply(page)
        if(ui.motion){StartupMotion.hero(page)}
        autoPage=page;return page
    }
    private fun enableAndActivate() {
        if(phone()){startAuto();return}
        DeveloperOptionsControl.changeFromGuide(this,true);refresh()
    }
    private fun openDebugging(){if(phone())AudioTools.openWirelessDebugging(this,704) else runCatching {startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS))}}
    private fun close(){content.removeCallbacks(steps);content.removeCallbacks(connect);content.removeCallbacks(retryAfterWifi);flow.cancel(keepReturn=WorkerConnection.ready);if(!WorkerConnection.ready)PowerActivationRequest.cancel();AppNavigation.closeSettings(this)}
    private fun watchWifi(on:Boolean) {
        val manager=getSystemService(ConnectivityManager::class.java)?:return
        if(on && !wifiWatching) {
            wifiWatching=runCatching {manager.registerNetworkCallback(NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI).build(),wifi)}.isSuccess
        } else if(!on && wifiWatching) {runCatching {manager.unregisterNetworkCallback(wifi)};wifiWatching=false}
    }
    override fun onResume(){super.onResume();flow.resume();watchWifi(true);if(!observing){listOf(Settings.Global.DEVELOPMENT_SETTINGS_ENABLED,Settings.Global.ADB_ENABLED,"adb_wifi_enabled").forEach {contentResolver.registerContentObserver(Settings.Global.getUriFor(it),false,settings)};observing=true};refresh()}
    override fun onPause(){if(observing){contentResolver.unregisterContentObserver(settings);observing=false};watchWifi(false);flow.pause();super.onPause()}
    override fun onSaveInstanceState(state:Bundle) {
        flow.save(state)
        state.putInt("auto",auto.ordinal);state.putInt("auto_failure",failure.ordinal);state.putInt("auto_message",failureMessage);state.putBoolean("auto_started",started)
        super.onSaveInstanceState(state)
    }
    override fun onRequestPermissionsResult(code:Int,permissions:Array<out String>,results:IntArray){super.onRequestPermissionsResult(code,permissions,results);flow.permissionsResult(code,results)}
    override fun onDestroy(){if(observing)contentResolver.unregisterContentObserver(settings);watchWifi(false);content.removeCallbacks(steps);content.removeCallbacks(connect);content.removeCallbacks(retryAfterWifi);flow.destroy();super.onDestroy()}
    @Deprecated("Legacy back dispatcher")override fun onBackPressed(){close()}
}
