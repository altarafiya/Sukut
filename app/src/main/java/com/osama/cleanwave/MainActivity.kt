package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.app.Dialog
import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView

/**
 * Home: a flat power control that says what Sukut is doing, the three filtering
 * modes as tiles, and one Playback row. Sized to fit a normal phone screen
 * without scrolling. Built once; refresh() only changes text, colours and
 * visibility of existing views.
 */
class MainActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var home: BounceScrollView
    private lateinit var hero: LinearLayout
    private lateinit var heroFill: GradientDrawable
    private lateinit var power: PowerButton
    private lateinit var statusText: TextView
    private lateinit var statusDetail: TextView
    private lateinit var statusAction: TextView
    private lateinit var overloadTip: View
    private lateinit var bufferingAction: TextView
    private lateinit var bluetoothNotice: TextView
    private lateinit var modeTiles: ModeTiles
    private lateinit var backgroundPanel: LinearLayout
    private lateinit var backgroundSlider: SeekBar
    private lateinit var backgroundValue: TextView
    private lateinit var qualityValue: TextView
    private lateinit var qualityDetail: TextView
    private lateinit var qualityRow: View
    private var paintedBuffer = -1
    private var trackingBackground = false
    private var modeHelpDialog: Dialog? = null
    private var performanceDialog: Dialog? = null
    private var reconnectDialog: Dialog? = null
    private var resumed = false
    private var appearance = ""
    private var selectionSignature = ""
    private var heroLit: Boolean? = null
    private val handler = Handler(Looper.getMainLooper())
    private val updateUi = Runnable { refresh() }
    private val expiry = Runnable { refresh() }
    private val applyAppearance = Runnable { if (!isFinishing && !isDestroyed) recreate() }
    private val preferencesListener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
        if (ControlPlanePolicy.affectsHome(key)) { handler.removeCallbacks(updateUi); handler.post(updateUi) }
    }
    private val binderReceived = WorkerConnection.OnChanged { if (resumed) handler.post(updateUi) }
    private val binderDead = WorkerConnection.OnChanged { if (resumed) handler.post(updateUi) }

    override fun attachBaseContext(base: Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state)
        if (state == null) StartupMotion.dismissLaunchWindow(this)
        appearance = UiPrefs.signature(this)
        CleanerPrefs.prepareNewEngine(this)

        ui = MobileUi(this); ui.styleBars()
        home = buildUi(); setContentView(home)
        WorkerConnection.addListener(binderReceived); WorkerConnection.addListener(binderDead)
        
        if (!UiPrefs.setupCompleted(this) && state == null) AppNavigation.openSetup(this)
        else if (intent.getBooleanExtra("reconnect", false)) {
            intent.removeExtra("reconnect"); home.post { reconnect() }
        }
    }
    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent); setIntent(intent)
        if(PowerActivationRequest.consumeIfReady()) PlaybackController.enable(this)
        if (intent.getBooleanExtra("reconnect", false)) { intent.removeExtra("reconnect"); reconnect() }
    }
    override fun onResume() {
        super.onResume()
        if(PowerActivationRequest.consumeIfReady()) PlaybackController.enable(this)
        resumed = true
        if (appearance != UiPrefs.signature(this)) {
            // Let the Settings pop complete before replacing Home for a new locale/theme.
            handler.removeCallbacks(applyAppearance); handler.postDelayed(applyAppearance, 280); return
        }
        CleanerPrefs.registerListener(this, preferencesListener)
        handler.removeCallbacks(expiry); handler.post(updateUi)
        if (UiPrefs.setupCompleted(this)) handler.postDelayed({ offerPermissionsAndTile() }, 700)
    }
    override fun onPause() {
        resumed = false
        CleanerPrefs.unregisterListener(this, preferencesListener)
        handler.removeCallbacks(expiry); handler.removeCallbacks(updateUi)
        handler.removeCallbacks(applyAppearance)
        super.onPause()
    }
    override fun onDestroy() {
        modeHelpDialog?.dismiss(); modeHelpDialog = null
        performanceDialog?.dismiss(); performanceDialog = null
        reconnectDialog?.dismiss(); reconnectDialog = null
        handler.removeCallbacksAndMessages(null)
        WorkerConnection.removeListener(binderReceived); WorkerConnection.removeListener(binderDead)
        
        super.onDestroy()
    }

    private fun buildUi(): BounceScrollView {
        val root = ui.root(if (ui.large) 1040 else 640)
        root.addView(LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(ui.dp(4), ui.dp(10), 0, 0)
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.VERTICAL
                addView(ui.title(R.string.app_name))
                addView(ui.text(R.string.beta_home_tagline, 13f, color = ui.secondary))
            }, LinearLayout.LayoutParams(0, -2, 1f))
            addView(ui.questionButton(R.string.audio_mode_help) {
                modeHelpDialog?.dismiss()
                modeHelpDialog = AudioHelp.showModes(this@MainActivity).also { dialog ->
                    dialog.setOnDismissListener { if (modeHelpDialog === dialog) modeHelpDialog = null }
                }
            }.apply { tag = "home_mode_help" }, LinearLayout.LayoutParams(ui.dp(44), ui.dp(44)).apply { marginEnd = ui.dp(8) })
            addView(ui.iconButton(R.drawable.ic_settings, R.string.settings_description) {
                AppNavigation.openSettings(this@MainActivity)
            }, LinearLayout.LayoutParams(ui.dp(44), ui.dp(44)))
        })
        root.addView(ui.space(if (ui.short) 12 else 18))
        heroCard()
        val side = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(modesSection())
            addView(ui.space(if (ui.short) 16 else 22))
            addView(performanceSection())
        }
        if (ui.large) {
            root.addView(LinearLayout(this).apply {
                gravity = Gravity.TOP
                addView(hero, LinearLayout.LayoutParams(0, -2, 1f))
                addView(side, LinearLayout.LayoutParams(0, -2, 1.15f).apply { marginStart = ui.dp(20) })
            })
        } else {
            root.addView(hero); root.addView(ui.space(if (ui.short) 16 else 22)); root.addView(side)
        }
        return ui.scroll(root).apply { id = R.id.home_scroll; tag = "home_screen" }
    }

    private fun heroCard() {
        heroFill = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(ui.cardColor, ui.cardColor))
            .apply { cornerRadius = ui.dp(28).toFloat() }
        hero = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; tag = "home_status"
            gravity = Gravity.CENTER_HORIZONTAL; background = heroFill
            val pad = ui.dp(if (ui.short) 14 else 20)
            setPadding(ui.dp(20), pad, ui.dp(20), pad + ui.dp(4))
        }
        power = PowerButton(this, ui.palette, if (ui.short) 112 else 128).apply {
            tag = "power_button"
            ui.springPress(this, .94f)
            setOnClickListener { ui.tap(this, strong = true); togglePower() }
        }
        hero.addView(power, LinearLayout.LayoutParams(-2, -2).apply { gravity = Gravity.CENTER_HORIZONTAL })
        statusText = centered(ui.text(R.string.off, 22f, true)).apply {
            accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
        }
        hero.addView(statusText, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(2) })
        statusDetail = centered(ui.text("", 14f, color = ui.secondary)).apply {
            setPadding(ui.dp(8), ui.dp(2), ui.dp(8), 0)
            setLineSpacing(ui.dp(2).toFloat(), 1f); maxLines = 3
        }
        hero.addView(statusDetail, LinearLayout.LayoutParams(-1, -2))
        statusAction = ui.button(R.string.reconnect_worker, R.drawable.ic_worker, MobileUi.Tone.FILLED) {
            if (!ready()) reconnect() else {
                CleanerPrefs.setStatus(this@MainActivity, "Off")
                PlaybackController.enable(this@MainActivity)
            }
        }
        hero.addView(statusAction, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(14) })
        overloadTip = ui.notice(ui.palette.warningContainer).apply {
            addView(ui.text(R.string.overload_tip, 15f, true, ui.warning))
            addView(ui.text(R.string.buffer_delay_warning, 13f, color = ui.warning).apply { setPadding(0, ui.dp(2), 0, ui.dp(10)) })
            bufferingAction = ui.button(R.string.use_balanced, R.drawable.ic_buffer, MobileUi.Tone.WARNING) {
                EnginePerformance.increaseBuffering(this@MainActivity); refresh()
            }.apply { tag = "increase_buffering"; background = ui.pressable(ui.cardColor, 14) }
            addView(bufferingAction)
        }
        hero.addView(overloadTip, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(14) })
        bluetoothNotice = ui.text(TextAccents.color(this, getString(R.string.bluetooth_output_notice)), 13f, color = ui.palette.onAccentContainer).apply {
            tag = "bluetooth_output_notice"; visibility = View.GONE
            accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            background = ui.rounded(ui.palette.accentContainer, 16)
            setPadding(ui.dp(14), ui.dp(10), ui.dp(14), ui.dp(10))
        }
        hero.addView(bluetoothNotice, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
    }

    private fun centered(view: TextView) = view.apply { gravity = Gravity.CENTER; textAlignment = View.TEXT_ALIGNMENT_CENTER }

    private fun modesSection(): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; tag = "audio_modes_card"
        addView(ui.sectionLabel(R.string.audio_mode))
        modeTiles = ModeTiles(this@MainActivity, AudioModes.filtering) { selectMode(it) }
        addView(modeTiles, LinearLayout.LayoutParams(-1, -2))
        addView(backgroundControl(), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
    }

    private fun backgroundControl(): View {
        backgroundPanel = ui.card(14).apply {
            visibility = View.GONE
            setPaddingRelative(ui.dp(16), ui.dp(12), ui.dp(16), ui.dp(6))
            addView(LinearLayout(this@MainActivity).apply {
                gravity = Gravity.CENTER_VERTICAL
                addView(ui.text(R.string.background_level, 14f, true), LinearLayout.LayoutParams(0, -2, 1f))
                backgroundValue = ui.text("", 14f, true, ui.accent).apply { textDirection = View.TEXT_DIRECTION_LTR }
                addView(backgroundValue)
            })
            backgroundSlider = TouchSeekBar(this@MainActivity).apply {
                max = 30; minimumHeight = ui.dp(64); minHeight = ui.dp(6); maxHeight = ui.dp(6); tag = "optimized_background"
                contentDescription = getString(R.string.background_level)
                progressTintList = ColorStateList.valueOf(ui.accent)
                progressBackgroundTintList = ColorStateList.valueOf(ui.dividerColor)
                thumbTintList = ColorStateList.valueOf(ui.accent)
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onStartTrackingTouch(bar: SeekBar?) { trackingBackground = true }
                    override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                        backgroundValue.text = db(progress - 20)
                        if (fromUser && progress == 20) bar?.performHapticFeedback(android.view.HapticFeedbackConstants.CLOCK_TICK)
                        // Keyboard and accessibility steps are completed actions.
                        if (fromUser && !trackingBackground) CleanerPrefs.setOptimizedBackground(this@MainActivity, progress - 20)
                    }
                    override fun onStopTrackingTouch(bar: SeekBar?) {
                        trackingBackground = false
                        CleanerPrefs.setOptimizedBackground(this@MainActivity, (bar?.progress ?: 10) - 20)
                    }
                })
            }
            SliderStyle.apply(backgroundSlider, ui, ModeStyle.audio(CleanerPrefs.SOURCE_OPTIMIZED, ui.dark))
            addView(backgroundSlider, LinearLayout.LayoutParams(-1, ui.dp(64)))
        }
        return backgroundPanel
    }

    private fun db(value: Int) = "${if (value > 0) "+" else ""}$value dB"

    private fun performanceSection(): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        addView(ui.sectionLabel(R.string.engine_performance))
        qualityValue = ui.text("", 16f, true)
        qualityDetail = ui.valueText("")
        val value = LinearLayout(this@MainActivity).apply {
            orientation = LinearLayout.VERTICAL
            addView(qualityValue); addView(qualityDetail.apply { setPadding(0, ui.dp(1), 0, 0) })
        }
        qualityRow = ui.row(R.drawable.ic_buffer, value) {
            performanceDialog?.dismiss()
            performanceDialog = EnginePerformance.show(this@MainActivity) { refresh() }
            performanceDialog?.setOnDismissListener { dismissed -> if (performanceDialog === dismissed) performanceDialog = null }
        }.apply { tag = "home_sound_quality"; ui.springPress(this, .98f) }
        addView(ui.group().apply { addView(qualityRow) })
    }

    private fun togglePower() {
        val status = CleanerPrefs.status(this)
        if (status == "Stopping") return
        if (CleanerPrefs.isEnabled(this) || CleanerPrefs.startPending(this)) PlaybackController.disable(this)
        else if (!ready()) {
            PowerActivationRequest.request()
            AppNavigation.openActivation(this,UiPrefs.activationMethod(this),LocalActivationService.ACTIVATE)
        }
        else {
            CleanerPrefs.setStatus(this, "Off")
            PlaybackController.enable(this)
        }
        refresh()
    }

    private fun selectMode(mode: Int) {
        if (CleanerPrefs.status(this) == "Stopping") return
        CleanerPrefs.setSourcePreset(this, mode)
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(this))
            startService(Intent(this, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
        refresh()
    }

    private fun reconnect() {
        if (ready() || isFinishing || isDestroyed || reconnectDialog?.isShowing == true) return
        if (!UiPrefs.setupCompleted(this)) { AppNavigation.openSetup(this); return }
        AppNavigation.openActivation(this)
    }
    private fun ready(): Boolean = when (WorkerBackend.availabilityState()) {
        WorkerBackend.Companion.Availability.READY -> true
        else -> false
    }

    private fun refresh() {
        if (!::statusText.isInitialized) return
        CleanerPrefs.reconcileRuntimeState(this)
        val enabled = CleanerPrefs.isEnabled(this); val status = CleanerPrefs.status(this); val detail = CleanerPrefs.error(this)
        val availability = WorkerBackend.availabilityState()
        val workerReady = ready()
        if (workerReady) { reconnectDialog?.dismiss(); reconnectDialog = null }
        val preset = CleanerPrefs.sourcePreset(this)
        val mode = AudioModes.mode(preset)
        val modeName = getString(mode.label)
        val recovering = enabled && detail.startsWith("AI temporarily bypassed")
        val recent = enabled && CleanerPrefs.recoveryUntil(this) > SystemClock.elapsedRealtime()
        val standby = enabled && detail == PrivilegedAudioService.STANDBY_STATUS
        val stopping = status == "Stopping"
        val starting = enabled && status == "Starting privileged audio route"
        val failed = !enabled && detail.isNotBlank() && status != "Off"
        val needsWorker = !workerReady && !enabled
        val label = when {
            needsWorker -> R.string.worker_not_running
            failed -> R.string.audio_issue
            stopping -> R.string.stopping
            starting -> R.string.starting
            recovering -> R.string.recovering
            recent -> R.string.recently_recovered
            standby -> R.string.status_standby
            enabled -> R.string.status_filtering
            else -> R.string.off
        }
        val color = when {
            failed -> ui.errorColor
            needsWorker || recovering || recent -> ui.warning
            else -> ui.primary
        }
        val statusLabel = getString(label)
        if (statusText.text.toString() != statusLabel) statusText.text = statusLabel
        statusText.setTextColor(color)
        val detailText: CharSequence = when {
            needsWorker -> getString(R.string.status_worker_help)
            failed -> detail
            recovering && CleanerPrefs.silentRecovery(this) -> getString(R.string.recovery_silent)
            recovering -> getString(R.string.recovery_original)
            recent -> getString(R.string.recovery_recent_detail)
            standby -> getString(R.string.status_standby_detail)
            enabled && !starting -> getString(R.string.status_filtering_detail, modeName)
            starting || stopping -> modeName
            else -> getString(R.string.home_tap_to_start, modeName)
        }
        if (statusDetail.text.toString() != detailText.toString()) statusDetail.text = detailText
        statusDetail.visibility = if (detailText.isBlank()) View.GONE else View.VISIBLE
        power.setModeColor(ModeStyle.audio(CleanerPrefs.sourcePreset(this), ui.dark))
        power.show(when {
            needsWorker -> PowerButton.State.OFF
            failed -> PowerButton.State.ATTENTION
            stopping || starting -> PowerButton.State.STARTING
            standby -> PowerButton.State.STANDBY
            enabled -> PowerButton.State.ON
            else -> PowerButton.State.OFF
        }, getString(if (enabled) R.string.power_turn_off else R.string.power_turn_on) + ", " + statusLabel)
        power.isEnabled = !stopping
        // Keep the hero neutral; the power button carries the active state.
        val lit = enabled && !standby && !starting && !stopping
        if (heroLit != lit) {
            heroLit = lit
            heroFill.colors = intArrayOf(ui.cardColor, ui.cardColor)
        }
        statusAction.visibility = if (failed && !needsWorker) View.VISIBLE else View.GONE
        statusAction.setText(if (needsWorker) R.string.reconnect_worker else R.string.retry_audio)

        val value = EnginePerformance.bufferLabel(this); val explanation = EnginePerformance.effectsSummary(this)
        if (qualityValue.text.toString() != value) qualityValue.text = value
        val bufferMode = CleanerPrefs.buffering(this)
        val bufferColor = ModeStyle.buffer(bufferMode, ui.dark)
        if (paintedBuffer != bufferMode) (qualityRow as android.widget.LinearLayout).getChildAt(0).let { icon ->
            (icon as android.widget.ImageView).setImageResource(ModeStyle.bufferIcon(bufferMode))
            icon.imageTintList = ColorStateList.valueOf(bufferColor)
            paintedBuffer = bufferMode
        }
        qualityValue.setTextColor(bufferColor)
        if (qualityDetail.text.toString() != explanation) qualityDetail.text = explanation
        qualityRow.contentDescription = getString(R.string.engine_performance) + ", " + value + ", " + explanation
        bluetoothNotice.visibility = if (enabled && CleanerPrefs.bluetoothOutput(this)) View.VISIBLE else View.GONE
        overloadTip.visibility = if (enabled && CleanerPrefs.overloadTip(this) && CleanerPrefs.buffering(this) < 2) View.VISIBLE else View.GONE
        bufferingAction.setText(if (CleanerPrefs.buffering(this) == 0) R.string.use_balanced else R.string.use_maximum)
        bufferingAction.contentDescription = bufferingAction.text

        backgroundPanel.visibility = if (preset == CleanerPrefs.SOURCE_OPTIMIZED) View.VISIBLE else View.GONE
        if (!trackingBackground) {
            val background = CleanerPrefs.optimizedBackground(this)
            backgroundSlider.progress = background + 20
            backgroundValue.text = db(background)
        }
        val signature = "$preset:$stopping"
        if (signature != selectionSignature) {
            selectionSignature = signature
            modeTiles.select(preset, !stopping)
        }
        handler.removeCallbacks(expiry)
        if (recent && !recovering) handler.postDelayed(expiry, (CleanerPrefs.recoveryUntil(this) - SystemClock.elapsedRealtime()).coerceAtLeast(1) + 10)
    }

    private fun offerPermissionsAndTile() {
        if (isFinishing || isDestroyed || !hasWindowFocus() || !UiPrefs.setupCompleted(this)) return
        val prefs = getSharedPreferences("cleanwave_state", MODE_PRIVATE)
        if (Build.VERSION.SDK_INT >= 33 && !prefs.getBoolean("notification_prompted_sukut", false)) {
            prefs.edit().putBoolean("notification_prompted_sukut", true).apply()
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 100); return
            }
        }
        if (Build.VERSION.SDK_INT >= 33 && !prefs.getBoolean("quick_tile_prompted_v1", false)) {
            prefs.edit().putBoolean("quick_tile_prompted_v1", true).apply()
            runCatching { getSystemService(StatusBarManager::class.java).requestAddTileService(
                ComponentName(this, CleanerTileService::class.java), getString(R.string.app_name),
                Icon.createWithResource(this, R.drawable.ic_sukut_tile), mainExecutor) { } }
        }
    }
}
