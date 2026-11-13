package com.osama.cleanwave

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.LinearLayout
import android.widget.FrameLayout

/** One resumable guide. Never opens another guide in response to a worker event. */
internal object WorkerGuide {
    const val PHONE = 0
    const val WINDOWS = 1
    class Controller(
        private val activity: Activity, state: Bundle?, initial: Int = UiPrefs.activationMethod(activity), private val showProgress: Boolean = true,
        private val pair: () -> Unit, private val cancelPair: () -> Unit, private val changed: () -> Unit
    ) {
        private val ui = MobileUi(activity)
        val view = FrameLayout(activity)
        private lateinit var content:LinearLayout
        var method = if(Build.VERSION.SDK_INT<30) WINDOWS else (state?.getInt("guide_method",initial)?:initial).coerceIn(PHONE,WINDOWS)
            private set
        var step = (state?.getInt("guide_step",-1)?:-1).coerceIn(-1,if(method==WINDOWS)4 else 3)
            private set
        private val steps get() = if(method==PHONE) (0..3).toList() else (0..4).toList()
        val progressCount get()=steps.size+1 // Choosing a method belongs inside Activation too.
        val progressIndex get()=if(step<0)0 else steps.indexOf(step)+1
        private var snapshot=""
        val canNext get() = when {
            step<0 -> true
            step==(if(method==WINDOWS)3 else 2) -> WorkerConnection.ready
            else -> true
        }
        val nextLabel get()=if(step==steps.last())R.string.guide_continue else R.string.next
        init {
            if(step>=0 && step !in steps) step=steps.firstOrNull {it>=step}?:steps.last()
            render(false,0)
        }
        fun save(state:Bundle) {state.putInt("guide_method",method);state.putInt("guide_step",step)}
        fun back():Boolean {
            if(step<0)return false
            cancelPair();step=steps.lastOrNull {it<step}?:-1;render(direction=-1);return true
        }
        fun next():Boolean {
            if(!canNext)return false
            if(step==steps.last()) {DeveloperOptionsControl.dismissCleanup(activity);if(!WorkerConnection.ready)PowerActivationRequest.cancel();return true}
            if(step<0)UiPrefs.setActivationMethod(activity,method)
            step=steps.firstOrNull {it>step}?:steps.last();render(direction=1);return false
        }
        fun refresh() {
            val now="${WorkerConnection.ready}:${WorkerConnection.debuggingTethered}:${DeveloperOptionsControl.available(activity)}:${DeveloperOptionsControl.developerEnabled(activity)}:${DeveloperOptionsControl.usbEnabled(activity)}:${DeveloperOptionsControl.wirelessEnabled(activity)}:${DeveloperOptionsControl.tileAdded(activity)}"
            if(now!=snapshot) {snapshot=now;render()}
        }
        private fun body(id:Int) {content.addView(ui.text(id,15f,color=ui.secondary).apply {setPadding(0,ui.dp(8),0,ui.dp(18));setLineSpacing(ui.dp(2).toFloat(),1f)})}
        private fun title(id:Int,icon:Int) {
            content.addView(ui.icon(icon,ui.accent,30),LinearLayout.LayoutParams(ui.dp(30),ui.dp(30)).apply {bottomMargin=ui.dp(12)})
            content.addView(ui.text(id,25f,true).apply {isAccessibilityHeading=true})
        }
        private fun action(id:Int,icon:Int=R.drawable.ic_settings,primary:Boolean=false,tag:String?=null,click:()->Unit) {
            content.addView(ui.button(id,icon,if(primary)MobileUi.Tone.FILLED else MobileUi.Tone.TONAL,click).apply {this.tag=tag},LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(12)})
        }
        private fun picture(p:GuidePictures.Picture) {content.addView(GuidePictures.button(activity,p),LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(8);bottomMargin=ui.dp(14)})}
        private fun openSettings() {runCatching {activity.startActivity(Intent(Settings.ACTION_APPLICATION_DEVELOPMENT_SETTINGS))}.onFailure {AudioTools.openAboutPhone(activity)}}
        private fun instruction(heading:Int,detail:Int,icon:Int,color:Int=ui.accent) {
            content.addView(ui.card(14).apply {
                addView(LinearLayout(activity).apply {
                    gravity=Gravity.CENTER_VERTICAL
                    addView(ui.icon(icon,color,22))
                    addView(ui.text(heading,16f,true,color),LinearLayout.LayoutParams(0,-2,1f).apply {marginStart=ui.dp(10)})
                })
                if(detail!=0)addView(ui.text(detail,14f,color=ui.secondary).apply {setPadding(0,ui.dp(8),0,0)})
            },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(12)})
        }
        private fun progress() {
            if(!showProgress)return
            content.addView(ui.text(activity.getString(R.string.guide_progress,progressIndex+1,progressCount),13f,true,ui.secondary).apply {tag="guide_step_counter";accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE},LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(10)})
            content.addView(LinearLayout(activity).apply {
                tag="activation_progress_segments";importantForAccessibility=View.IMPORTANT_FOR_ACCESSIBILITY_NO
                repeat(progressCount) {i->addView(View(activity).apply {isSelected=i<=progressIndex;background=ui.rounded(if(i<=progressIndex)ui.accent else ui.dividerColor,2)},LinearLayout.LayoutParams(0,ui.dp(4),1f).apply {if(i>0)marginStart=ui.dp(5)})}
            },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(18)})
        }
        private fun statuses(vararg indices:Int) {
            content.addView(ActivationStatus.card(activity,*indices),LinearLayout.LayoutParams(-1,-2).apply {topMargin=ui.dp(12);bottomMargin=ui.dp(20)})
        }
        private fun choice(value:Int,title:Int,detail:Int,icon:Int) {
            val selected=method==value
            content.addView(ui.card(18).apply {
                tag=if(value==WINDOWS)"guide_choose_computer" else "guide_choose_phone"
                background=ui.rounded(if(selected)ui.selectedColor else ui.cardColor,20).apply {setStroke(ui.dp(if(selected)2 else 1),if(selected)ui.accent else ui.dividerColor)}
                isSelected=selected;isClickable=true;isFocusable=true
                accessibilityDelegate=object:View.AccessibilityDelegate() {override fun onInitializeAccessibilityNodeInfo(host:View,info:AccessibilityNodeInfo) {super.onInitializeAccessibilityNodeInfo(host,info);info.className="android.widget.RadioButton";info.isCheckable=true;info.isChecked=selected}}
                contentDescription=activity.getString(title)+". "+activity.getString(detail)
                addView(LinearLayout(activity).apply {gravity=Gravity.CENTER_VERTICAL;addView(ui.icon(icon,if(selected)ui.accent else ui.secondary,26));addView(ui.text(title,19f,true),LinearLayout.LayoutParams(0,-2,1f).apply {marginStart=ui.dp(12)})})
                if(value==WINDOWS)addView(ui.text(R.string.guide_recommended,12f,true,ui.accent).apply {setPadding(0,ui.dp(10),0,0)})
                addView(ui.text(detail,14f,color=ui.secondary).apply {setPadding(0,ui.dp(8),0,0)})
                ui.springPress(this)
                setOnClickListener {if(value==PHONE && Build.VERSION.SDK_INT<30)return@setOnClickListener;ui.tap(this);cancelPair();method=value;UiPrefs.setActivationMethod(activity,value);render()}
                if(value==PHONE && Build.VERSION.SDK_INT<30){isEnabled=false;alpha=.55f}
            },LinearLayout.LayoutParams(-1,-2).apply {bottomMargin=ui.dp(14)})
        }
        private fun render(notify:Boolean=true,direction:Int=0) {
            content=LinearLayout(activity).apply {orientation=LinearLayout.VERTICAL}
            view.tag=if(method==WINDOWS)"guide_windows" else "guide_phone"
            progress()
            if(step<0) {
                title(R.string.guide_choose_title,R.drawable.ic_worker);body(R.string.guide_choose_intro)
                choice(WINDOWS,R.string.guide_method_windows,R.string.guide_computer_benefit,R.drawable.ic_setup_pc)
                choice(PHONE,R.string.guide_method_phone,if(Build.VERSION.SDK_INT>=30)R.string.guide_phone_benefit else R.string.guide_wireless_min_api,R.drawable.ic_phone)
            } else {
                when {
                    step==0 -> {
                        title(R.string.guide_dev_title,R.drawable.ic_developer_tile)
                        statuses(0)
                        if(!DeveloperOptionsControl.developerEnabled(activity) && method==WINDOWS && DeveloperOptionsControl.available(activity)) {
                            body(R.string.guide_developer_body)
                            action(R.string.dev_enable_usb,R.drawable.ic_developer_tile,true) {DeveloperOptionsControl.changeFromGuide(activity,true);refresh()}
                        } else if(!DeveloperOptionsControl.developerEnabled(activity)) {body(R.string.guide_developer_body);action(R.string.guide_about_phone,R.drawable.ic_phone){AudioTools.openAboutPhone(activity)}}
                    }
                    method==WINDOWS -> computerStep()
                    else -> phoneStep()
                }
            }
            SukutTypography.apply(content)
            StartupMotion.slidePage(view,content,direction,wrapContent=true)
            if(direction!=0)view.post {var p=view.parent;while(p!=null){if(p is android.widget.ScrollView){p.scrollTo(0,0);break};p=p.parent}}
            if(notify)changed()
        }
        private fun computerStep() {when(step) {
            1 -> {title(R.string.guide_usb_title,R.drawable.ic_phone);statuses(1);if(!DeveloperOptionsControl.usbEnabled(activity)){body(R.string.guide_windows_1_body);action(R.string.guide_developer_options){openSettings()};picture(GuidePictures.Picture.DEVELOPER)}}
            2 -> {title(R.string.guide_connect_title,R.drawable.ic_setup_pc);body(R.string.guide_windows_2_body)}
            3 -> {
                title(R.string.guide_activate_title,R.drawable.ic_worker)
                statuses(2)
                if(!WorkerConnection.ready) {
                    instruction(R.string.route_open_computer,R.string.route_open_computer_body,R.drawable.ic_setup_pc)
                    instruction(R.string.route_phone,R.string.route_phone_body,R.drawable.ic_phone,ui.warning)
                    instruction(R.string.route_activate,R.string.route_activate_body,R.drawable.ic_worker)
                }
            }
            4 -> {
                title(if(WorkerConnection.ready)R.string.guide_phone_ready_title else R.string.status_activation_needed,R.drawable.ic_worker)
                statuses(0,1,2)
                if(WorkerConnection.ready)instruction(R.string.ready_start,R.string.ready_start_body,R.drawable.ic_power,ui.readyColor)
                else body(R.string.worker_needed_detail)
            }
        }}
        private fun phoneStep() {when(step) {
            1 -> {title(R.string.guide_code_title,R.drawable.ic_pair_key);body(R.string.guide_phone_1_body);picture(GuidePictures.Picture.NOTIFICATION);action(R.string.guide_watch_video,R.drawable.ic_tutorial_video){AudioTools.website(activity,ReleaseLinks.TUTORIAL_VIDEO)}}
            2 -> {title(R.string.guide_pair_title,R.drawable.ic_setup_wireless);statuses(3,2);if(!WorkerConnection.ready){body(R.string.guide_phone_2_body);action(R.string.local_pair_button,R.drawable.ic_pair_key,true,"guide_pair_phone",pair);picture(GuidePictures.Picture.WIRELESS)}}
            3 -> {
                title(if(WorkerConnection.ready)R.string.guide_phone_ready_title else R.string.status_activation_needed,R.drawable.ic_worker)
                statuses(0,3,2)
                if(WorkerConnection.ready)instruction(R.string.ready_start,R.string.ready_start_body,R.drawable.ic_power,ui.readyColor) else body(R.string.worker_needed_detail)
                instruction(R.string.ready_reconnect,R.string.ready_reconnect_body,R.drawable.ic_setup_wireless)
                instruction(R.string.shortcut_optional_title,R.string.shortcut_optional_detail,R.drawable.ic_developer_tile,ui.warning)
                if(DeveloperOptionsControl.available(activity) && Build.VERSION.SDK_INT>=33)action(R.string.dev_add_tile,R.drawable.ic_developer_tile) {DeveloperOptionsControl.requestTile(activity){refresh()}}
            }
        }}
    }
}
