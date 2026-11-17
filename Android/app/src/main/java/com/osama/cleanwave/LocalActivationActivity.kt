package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView

/** The same one-step activation guide as onboarding, with a fixed footer. */
class LocalActivationActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var status: TextView
    private lateinit var notificationAction: View
    private lateinit var next: TextView
    private lateinit var flow: LocalActivationFlow
    private lateinit var guide: WorkerGuide.Controller
    override fun attachBaseContext(base: Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state)
        AppNavigation.registerActivationHost(this); AppNavigation.configurePage(this)
        ui = MobileUi(this); ui.styleBars()
        flow = LocalActivationFlow(this, state, { refresh() }, { refresh() })
        val screen = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setBackgroundColor(ui.background)
            setOnApplyWindowInsetsListener { view, insets ->
                val bars = MobileUi.systemBars(insets)
                view.setPadding(bars[0], bars[1], bars[2], bars[3]); insets
            }
        }
        val content = AdaptiveColumn(this, ui.dp(680)).apply {
            orientation = LinearLayout.VERTICAL; setPadding(ui.dp(20), ui.dp(20), ui.dp(20), ui.dp(16))
        }
        status = ui.text("", 14f, true, ui.accent).apply { setPadding(0, 0, 0, ui.dp(16)) }
        notificationAction = flow.notificationButton(ui)
        content.addView(status); content.addView(notificationAction)
        val method = intent.getIntExtra("method", UiPrefs.activationMethod(this))
        val action = intent.getStringExtra("activation_action")
        val restored = state ?: Bundle().apply {
            val requested=intent.getIntExtra("guide_start_step",-1)
            if(requested>=0) {putInt("guide_step",requested);putInt("guide_method",method)}
            else if (method == WorkerGuide.PHONE && action != null) { putInt("guide_step", 2); putInt("guide_method", method) }
        }
        guide = WorkerGuide.Controller(this, restored, initial = method,
            pair = { flow.begin(LocalActivationService.PAIR, true) }, cancelPair = { flow.cancel() }, changed = { refresh() })
        content.addView(guide.view)
        screen.addView(ui.scroll(content), LinearLayout.LayoutParams(-1, 0, 1f))
        val footer = AdaptiveColumn(this, ui.dp(680)).apply {
            orientation = LinearLayout.HORIZONTAL; setPadding(ui.dp(20), ui.dp(10), ui.dp(20), ui.dp(18))
            addView(ui.button(R.string.back, 0, MobileUi.Tone.PLAIN) { back() }, LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = ui.dp(10) })
            next = ui.button(R.string.next, 0, MobileUi.Tone.FILLED) { if (guide.next()) goHome() else refresh() }
            addView(next, LinearLayout.LayoutParams(0, -2, 1.4f))
        }
        screen.addView(FrameLayout(this).apply { addView(footer, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER_HORIZONTAL)) })
        setContentView(screen); SukutTypography.apply(screen)
        AppNavigation.handleBack(this) { back() }
        if (state == null && method == WorkerGuide.PHONE) {
            if (action == LocalActivationService.PAIR) flow.begin(action, true)
            else if (action == LocalActivationService.ACTIVATE) flow.begin(action)
        }
        refresh()
    }
    private fun refresh() {
        if (!::guide.isInitialized || !::next.isInitialized) return
        guide.refresh()
        notificationAction.visibility = if (flow.notificationsBlocked) View.VISIBLE else View.GONE
        status.visibility = View.GONE
        status.setText(if (flow.notificationsBlocked) R.string.local_notifications_needed else if (WorkerConnection.ready) R.string.worker_ready_short
            else LocalActivationState.message.takeIf { it != 0 } ?: R.string.worker_not_running)
        next.setText(guide.nextLabel); next.isEnabled = guide.canNext; next.alpha = if (next.isEnabled) 1f else .45f
    }
    private fun back() { if (!guide.back()) cancelAndClose() }
    private fun goHome() {
        flow.cancel()
        if (intent.getBooleanExtra("return_to_setup", false)) { finish(); return }
        AppNavigation.finishActivation(this)
    }
    private fun cancelAndClose() { DeveloperOptionsControl.dismissCleanup(this); PowerActivationRequest.cancel(); flow.cancel(); finish() }
    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, permissions, results); flow.permissionsResult(code, results)
    }
    override fun onSaveInstanceState(state: Bundle) { flow.save(state); guide.save(state); super.onSaveInstanceState(state) }
    override fun onNewIntent(intent: Intent) { super.onNewIntent(intent); setIntent(intent); refresh() }
    override fun onResume() { super.onResume(); flow.resume(); refresh() }
    override fun onPause() { flow.pause(); super.onPause() }
    override fun onDestroy() {
        AppNavigation.unregisterActivationHost(this); flow.destroy(); super.onDestroy() }
    @Deprecated("Legacy back dispatcher") override fun onBackPressed() { back() }
}
