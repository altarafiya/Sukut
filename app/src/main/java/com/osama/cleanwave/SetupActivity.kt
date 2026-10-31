package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

/** USB activation and sound setup. */
class SetupActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var pages: FrameLayout
    private lateinit var navigationHost: FrameLayout
    private lateinit var stepCounter: TextView
    private val progressBars=mutableListOf<View>()
    private var page = 0
    private var appearance = ""
    private var connectionStatus: TextView? = null
    private var nextButton: View? = null
    private lateinit var activation: LocalActivationFlow
    private var notificationAction: View? = null
    private val received = WorkerConnection.OnChanged { refreshConnection() }
    private val dead = WorkerConnection.OnChanged { refreshConnection() }
    override fun attachBaseContext(base: Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state)
        AppNavigation.configurePage(this)
        page = (state?.getInt("page") ?: intent.getIntExtra("page", 0)).coerceIn(0, 3)
        appearance = UiPrefs.signature(this)
        ui = MobileUi(this); ui.styleBars()
        activation = LocalActivationFlow(this, state, { refreshConnection() }, { refreshConnection() })
        val screen=LinearLayout(this).apply {
            orientation=LinearLayout.VERTICAL; setBackgroundColor(ui.background)
            setOnApplyWindowInsetsListener { view,insets ->
                val bars=MobileUi.systemBars(insets)
                view.setPadding(bars[0],bars[1],bars[2],bars[3]+ui.dp(8)); insets
            }
        }
        val heading=AdaptiveColumn(this,ui.dp(680)).apply {
            tag="setup_progress_header"; orientation=LinearLayout.VERTICAL
            setPadding(ui.dp(20),ui.dp(16),ui.dp(20),ui.dp(12))
            stepCounter=ui.text("",13f,true,ui.secondary).apply {
                tag="setup_step_counter"; accessibilityLiveRegion=View.ACCESSIBILITY_LIVE_REGION_POLITE
            }; addView(stepCounter)
            addView(LinearLayout(this@SetupActivity).apply {
                importantForAccessibility=View.IMPORTANT_FOR_ACCESSIBILITY_NO
                repeat(4) { index ->
                    val bar=View(this@SetupActivity).apply { background=ui.rounded(ui.dividerColor,2) }
                    progressBars.add(bar)
                    addView(bar,LinearLayout.LayoutParams(0,ui.dp(4),1f).apply { if(index>0)marginStart=ui.dp(6) })
                }
            },LinearLayout.LayoutParams(-1,-2).apply { topMargin=ui.dp(10) })
        }
        screen.addView(FrameLayout(this).apply { addView(heading,FrameLayout.LayoutParams(-1,-2,Gravity.CENTER_HORIZONTAL)) })
        pages=FrameLayout(this).apply { tag="setup_pages"; setBackgroundColor(ui.background) }
        screen.addView(pages,LinearLayout.LayoutParams(-1,0,1f))
        navigationHost=FrameLayout(this).apply { tag="setup_navigation"; setBackgroundColor(ui.background) }
        screen.addView(navigationHost,LinearLayout.LayoutParams(-1,-2))
        setContentView(screen)
        render()
        AppNavigation.handleBack(this) { back() }
        WorkerConnection.addListener(received); WorkerConnection.addListener(dead)
        
    }
    @Deprecated("Android 12 and earlier; newer versions use AppNavigation.handleBack")
    @Suppress("DEPRECATION")
    override fun onBackPressed() = back()
    override fun onSaveInstanceState(state: Bundle) { state.putInt("page", page); activation.save(state); super.onSaveInstanceState(state) }
    override fun onResume() {
        super.onResume()
        if (appearance != UiPrefs.signature(this)) { recreate(); return }
        activation.resume()
        refreshConnection()
    }
    override fun onPause() { activation.pause(); super.onPause() }
    override fun onRequestPermissionsResult(code: Int, permissions: Array<out String>, results: IntArray) {
        super.onRequestPermissionsResult(code, permissions, results); activation.permissionsResult(code, results)
    }
    override fun onDestroy() {
        WorkerConnection.removeListener(received); WorkerConnection.removeListener(dead)
        activation.destroy(); super.onDestroy()
    }
    private fun back() { if(page==1)activation.cancel(); if (page > 0) { page--; render(-1) } else AppNavigation.closeSetup(this) }

    private fun render(direction: Int = 0) {
        connectionStatus = null; nextButton = null; notificationAction = null
        stepCounter.text=getString(R.string.setup_step,page+1,4)
        progressBars.forEachIndexed { index,bar -> bar.background.setTint(if(index<=page)ui.accent else ui.dividerColor) }
        val root = AdaptiveColumn(this, ui.dp(680)).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(ui.dp(20), ui.dp(12), ui.dp(20), ui.dp(16))
        }

        val title = intArrayOf(R.string.setup_welcome, R.string.setup_worker_title, R.string.setup_sound_title, R.string.setup_ready_title)[page]
        val body = intArrayOf(R.string.setup_welcome_body, R.string.setup_worker_body, R.string.setup_sound_body, R.string.setup_ready_body)[page]
        if (page == 0) {
            root.addView(ui.brandIcon(72), LinearLayout.LayoutParams(ui.dp(72), ui.dp(72)).apply { bottomMargin = ui.dp(20) })
        }
        root.addView(ui.text(getString(title), 30f, true).apply {
            isAccessibilityHeading = true; includeFontPadding = true
        }, LinearLayout.LayoutParams(-1, -2))
        root.addView(ui.text(TextAccents.color(this, getString(body)), 16f, color = ui.secondary).apply {
            setPadding(0, ui.dp(8), 0, ui.dp(24)); setLineSpacing(ui.dp(3).toFloat(), 1f)
        })
        when (page) {
            0 -> root.addView(ui.card(16).apply {
                addView(ui.text(R.string.setup_language, 15f, true))
                val languages = arrayOf(UiPrefs.LANGUAGE_SYSTEM, UiPrefs.LANGUAGE_ENGLISH, UiPrefs.LANGUAGE_ARABIC)
                addView(SegmentedChoice(this@SetupActivity,
                    listOf(getString(R.string.system_default), getString(R.string.english_native), getString(R.string.arabic_native)),
                    languages.indexOf(UiPrefs.language(this@SetupActivity)).coerceAtLeast(0)) { which ->
                    UiPrefs.setLanguage(this@SetupActivity, languages[which]); recreate()
                }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
            })
            1 -> root.addView(LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                connectionStatus = ui.text("", 15f, true, ui.warning).also { addView(it) }
                notificationAction = activation.notificationButton(ui).also { addView(it) }
                addView(ui.space(12))
                WorkerGuide.add(this@SetupActivity, this, { activation.begin(LocalActivationService.PAIR, true) }, { activation.cancel() })
            })
            2 -> root.addView(ui.card(12).apply {
                addView(ui.text(R.string.performance_help, 14f, color = ui.secondary).apply { setPaddingRelative(ui.dp(6), ui.dp(4), ui.dp(6), ui.dp(12)) })
                addView(EnginePerformance.choices(this@SetupActivity))
            })
            3 -> {
                root.addView(AudioHelp.modeCards(this))
                root.addView(ui.text(R.string.setup_model_note, 13f, color = ui.secondary).apply { setPaddingRelative(ui.dp(8), ui.dp(12), ui.dp(8), 0) })
                root.addView(ui.text("ادعوا لخالي بالرَّحمة، ولزوجته بالشِّفاء،\nولجميع المسلمين.", 17f, color = ui.palette.highlight).apply {
                    textDirection = View.TEXT_DIRECTION_RTL
                    textAlignment = View.TEXT_ALIGNMENT_CENTER
                    gravity = Gravity.CENTER
                    includeFontPadding = true
                    setPaddingRelative(ui.dp(8), ui.dp(28), ui.dp(8), ui.dp(10))
                }, LinearLayout.LayoutParams(-1, -2))
            }
        }
        val navigation = AdaptiveColumn(this, ui.dp(680)).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(ui.dp(20), ui.dp(10), ui.dp(20), ui.dp(10))
            if (page > 0) addView(ui.button(R.string.back, 0, MobileUi.Tone.PLAIN) { back() },
                LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = ui.dp(10) })
            nextButton = ui.button(if (page == 3) R.string.finish_setup else R.string.next, 0, MobileUi.Tone.FILLED) {
                if (page == 3) {
                    UiPrefs.finishSetup(this@SetupActivity)
                    startActivity(Intent(this@SetupActivity, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
                    AppNavigation.closeSetup(this@SetupActivity)
                } else { if(page==1)activation.cancel(); page++; render(1) }
            }
            addView(nextButton, LinearLayout.LayoutParams(0, -2, if (page > 0) 1.4f else 1f))
        }
        navigationHost.removeAllViews()
        navigationHost.addView(navigation,FrameLayout.LayoutParams(-1,-2,Gravity.CENTER_HORIZONTAL))
        StartupMotion.slidePage(pages,ui.scroll(root),direction)
        refreshConnection()
    }

    private fun refreshConnection() {
        if (page != 1) return
        val ready = WorkerConnection.ready
        val message=if(activation.notificationsBlocked) R.string.local_notifications_needed else if(ready) R.string.worker_ready_short
            else LocalActivationState.message.takeIf { it!=0 } ?: R.string.setup_worker_required
        connectionStatus?.text = getString(message)
        notificationAction?.visibility = if(activation.notificationsBlocked) View.VISIBLE else View.GONE
        connectionStatus?.setTextColor(if (ready) ui.readyColor else ui.warning)
        nextButton?.isEnabled = ready; nextButton?.alpha = if (ready) 1f else .45f
    }
}
