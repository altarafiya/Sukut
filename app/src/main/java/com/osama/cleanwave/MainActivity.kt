package com.osama.cleanwave

import android.Manifest
import android.app.Activity
import android.app.StatusBarManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.drawable.Icon
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import rikka.shizuku.Shizuku

class MainActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var home: ScrollView
    private lateinit var statusDot: View
    private lateinit var statusText: TextView
    private lateinit var statusDetail: TextView
    private lateinit var statusAction: View
    private lateinit var overloadTip: View
    private lateinit var bufferingAction: TextView
    private lateinit var backgroundPanel: LinearLayout
    private lateinit var backgroundSlider: SeekBar
    private lateinit var backgroundValue: TextView
    private var trackingBackground = false
    private lateinit var modeDescription: TextView
    private lateinit var qualityValue: TextView
    private lateinit var bluetoothNotice: TextView
    private var performanceDialog: android.app.Dialog? = null
    private val buttons = mutableMapOf<Int, LinearLayout>()
    private val icons = mutableMapOf<Int, ImageView>()
    private val labels = mutableMapOf<Int, TextView>()
    private var appearance = ""
    private var selectionSignature = ""
    private var detailSignature = ""
    private var lastStatusColor = 0
    private var backCallback: OnBackInvokedCallback? = null
    private val handler = Handler(Looper.getMainLooper())
    private val updateUi = Runnable { refresh() }
    private val expiry = Runnable { refresh() }
    private val applyAppearance = Runnable { if (!isFinishing && !isDestroyed) recreate() }
    private val preferencesListener = SharedPreferences.OnSharedPreferenceChangeListener { _, _ ->
        handler.removeCallbacks(updateUi); handler.post(updateUi)
    }
    private val binderReceived = Shizuku.OnBinderReceivedListener { handler.post(updateUi) }
    private val binderDead = Shizuku.OnBinderDeadListener { handler.post(updateUi) }

    override fun attachBaseContext(base: Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state)
        if (state == null) StartupMotion.dismissLaunchWindow(this)
        appearance = UiPrefs.signature(this)
        CleanerPrefs.prepareNewEngine(this)

        ui = MobileUi(this); ui.styleBars()
        home = buildUi(); setContentView(home)
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = OnBackInvokedCallback { home.smoothScrollTo(0, 0) }.also {
                onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, it)
            }
        }
        Shizuku.addBinderReceivedListenerSticky(binderReceived); Shizuku.addBinderDeadListener(binderDead)
        if (!UiPrefs.setupCompleted(this) && state == null) startActivity(Intent(this, SetupActivity::class.java))
    }
    override fun onResume() {
        super.onResume()
        if (appearance != UiPrefs.signature(this)) {
            // Let the Settings pop complete before replacing Home for a new locale/theme.
            handler.removeCallbacks(applyAppearance); handler.postDelayed(applyAppearance, 280); return
        }
        CleanerPrefs.registerListener(this, preferencesListener)
        handler.removeCallbacks(expiry); handler.post(updateUi)
        if (UiPrefs.setupCompleted(this)) handler.postDelayed({ offerPermissionsAndTile() }, 700)
    }
    override fun onPause() {
        CleanerPrefs.unregisterListener(this, preferencesListener)
        handler.removeCallbacks(expiry); handler.removeCallbacks(updateUi)
        handler.removeCallbacks(applyAppearance)
        super.onPause()
    }
    override fun onDestroy() {
        performanceDialog?.dismiss(); performanceDialog = null
        handler.removeCallbacksAndMessages(null)
        Shizuku.removeBinderReceivedListener(binderReceived); Shizuku.removeBinderDeadListener(binderDead)
        if (Build.VERSION.SDK_INT >= 33) backCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        super.onDestroy()
    }
    @Suppress("DEPRECATION") override fun onBackPressed() { home.smoothScrollTo(0, 0) }

    private fun buildUi(): ScrollView {
        val root = ui.root(if (ui.large) 1040 else 760)
        root.addView(LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            addView(ui.text(R.string.app_name, 28f, true).apply { isAccessibilityHeading = true }, LinearLayout.LayoutParams(0, -2, 1f))
            addView(ImageButton(this@MainActivity).apply {
                setImageResource(R.drawable.ic_settings); imageTintList = ColorStateList.valueOf(ui.primary)
                background = ui.circle(ui.cardColor); contentDescription = getString(R.string.settings_description)
                setPadding(ui.dp(10), ui.dp(10), ui.dp(10), ui.dp(10))
                setOnClickListener { AppNavigation.openSettings(this@MainActivity) }
            }, LinearLayout.LayoutParams(ui.dp(44), ui.dp(44)))
        })
        root.addView(ui.space(18))
        val status = statusCard()
        val modes = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; addView(modesCard()); addView(ui.space(12)); addView(soundQualityCard())
            bluetoothNotice = ui.text(TextAccents.color(this@MainActivity, getString(R.string.bluetooth_output_notice)), 13f, color = ui.warning).apply {
                tag = "bluetooth_output_notice"; setPadding(ui.dp(12), ui.dp(8), ui.dp(12), 0)
                visibility = View.GONE; accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE
            }
            addView(bluetoothNotice)
        }
        if (ui.large) {
            root.addView(LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL; gravity = Gravity.TOP
                addView(modes, LinearLayout.LayoutParams(0, -2, 1.55f))
                addView(status, LinearLayout.LayoutParams(0, -2, 1f).apply { marginStart = ui.dp(16) })
            })
        } else {
            root.addView(status); root.addView(ui.space(12)); root.addView(modes)
        }
        SukutTypography.apply(root)
        return ui.scroll(root).apply { tag = "home_screen" }
    }

    private fun statusCard(): View = ui.card().apply {

        addView(LinearLayout(this@MainActivity).apply {
            gravity = Gravity.CENTER_VERTICAL
            statusDot = View(this@MainActivity).apply { background = ui.circle(ui.secondary) }
            addView(statusDot, LinearLayout.LayoutParams(ui.dp(9), ui.dp(9)).apply { marginEnd = ui.dp(10) })
            statusText = ui.text(R.string.off, 16f, true).apply { accessibilityLiveRegion = View.ACCESSIBILITY_LIVE_REGION_POLITE }
            addView(statusText, LinearLayout.LayoutParams(0, -2, 1f))
        })
        statusDetail = ui.text("", 13f, color = ui.secondary).apply { setPadding(0, ui.dp(6), 0, 0) }
        addView(statusDetail)
        statusAction = ui.button(R.string.run_setup, R.drawable.ic_system_theme) {
            startActivity(Intent(this@MainActivity, SetupActivity::class.java).putExtra("page", 1))
        }
        addView(statusAction, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
        overloadTip = ui.card(14).apply {
            background = ui.rounded(if (ui.dark) 0xFF3E3220.toInt() else 0xFFFFF2D9.toInt(), 16)
            addView(ui.text(R.string.overload_tip, 15f, true, ui.warning))
            addView(ui.text(R.string.buffer_delay_warning, 13f, color = ui.warning).apply { setPadding(0, ui.dp(6), 0, ui.dp(10)) })
            val action = ui.button(R.string.use_balanced, R.drawable.ic_buffer) { EnginePerformance.increaseBuffering(this@MainActivity); refresh() }
            bufferingAction = (action as ViewGroup).getChildAt(0) as TextView
            action.tag = "increase_buffering"
            addView(action)
        }
        addView(overloadTip, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
    }

    private fun modesCard(): View = ui.card().apply {
        tag = "audio_modes_card"
        addView(ui.text(R.string.audio_mode, 18f, true).apply { gravity = Gravity.CENTER; isAccessibilityHeading = true })
        addView(ui.space(14))
        val width = resources.configuration.screenWidthDp
        val columns = if (resources.configuration.fontScale > 1.3f || width < 600 || ui.large && width < 1000) 2 else 4
        AudioModes.modes.chunked(columns).forEachIndexed { rowIndex, row ->
            addView(LinearLayout(this@MainActivity).apply {
                orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER
                row.forEachIndexed { index, mode ->
                    addView(modeButton(mode), LinearLayout.LayoutParams(0, -1, 1f).apply { if (index > 0) marginStart = ui.dp(10) })
                }
            }, LinearLayout.LayoutParams(-1, ui.dp(if (columns == 4) 104 else 96)).apply { if (rowIndex > 0) topMargin = ui.dp(10) })
        }
        backgroundPanel = LinearLayout(this@MainActivity).apply {
            orientation = LinearLayout.VERTICAL; visibility = View.GONE
            addView(ui.space(14))
            addView(LinearLayout(this@MainActivity).apply {
                gravity = Gravity.CENTER_VERTICAL
                addView(ui.text(R.string.background_level, 14f, true), LinearLayout.LayoutParams(0, -2, 1f))
                backgroundValue = ui.text("", 14f, true, AudioModes.mode(CleanerPrefs.SOURCE_OPTIMIZED).color).apply { textDirection = View.TEXT_DIRECTION_LTR }
                addView(backgroundValue)
            })
            backgroundSlider = SeekBar(this@MainActivity).apply {
                max = 30; minHeight = ui.dp(48); tag = "optimized_background"
                contentDescription = getString(R.string.background_level)
                progressTintList = ColorStateList.valueOf(AudioModes.mode(CleanerPrefs.SOURCE_OPTIMIZED).color)
                thumbTintList = progressTintList
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onStartTrackingTouch(bar: SeekBar?) { trackingBackground = true }
                    override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                        val value = progress - 20
                        backgroundValue.text = "${if (value > 0) "+" else ""}$value dB"
                        if (fromUser && !trackingBackground) CleanerPrefs.setOptimizedBackground(this@MainActivity, value)
                    }
                    override fun onStopTrackingTouch(bar: SeekBar?) {
                        trackingBackground = false
                        CleanerPrefs.setOptimizedBackground(this@MainActivity, (bar?.progress ?: 10) - 20)
                    }
                })
            }
            addView(backgroundSlider, LinearLayout.LayoutParams(-1, -2))
        }
        addView(backgroundPanel)
        addView(modeInfoCard(), LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(14) })
    }

    private fun soundQualityCard(): View = ui.card(16).apply {
        tag = "home_sound_quality"
        orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
        minimumHeight = ui.dp(64)
        background = android.graphics.drawable.RippleDrawable(ColorStateList.valueOf(0x233482FF),
            ui.rounded(ui.cardColor), ui.rounded(Color.WHITE))
        isClickable = true; isFocusable = true
        accessibilityDelegate = object : View.AccessibilityDelegate() {
            override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                super.onInitializeAccessibilityNodeInfo(host, info); info.className = "android.widget.Button"
            }
        }
        addView(ImageView(this@MainActivity).apply {
            setImageResource(R.drawable.ic_buffer); imageTintList = ColorStateList.valueOf(ui.accent)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(ui.dp(22), ui.dp(22)).apply { marginEnd = ui.dp(12) })
        addView(ui.text(R.string.engine_performance, 15f, true, ui.primary).apply {
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(0, -2, 1f))
        qualityValue = ui.text("", 15f, true, ui.accent).apply {
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            gravity = Gravity.END or Gravity.CENTER_VERTICAL
        }
        // Both texts can wrap at large font sizes; neither can squeeze the other out.
        addView(qualityValue, LinearLayout.LayoutParams(0, -2, .85f))
        addView(ImageView(this@MainActivity).apply {
            setImageResource(R.drawable.ic_expand_more); imageTintList = ColorStateList.valueOf(ui.secondary)
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }, LinearLayout.LayoutParams(ui.dp(20), ui.dp(20)).apply { marginStart = ui.dp(8) })
        setOnClickListener {
            performanceDialog?.dismiss()
            performanceDialog = EnginePerformance.show(this@MainActivity) { refresh() }
            performanceDialog?.setOnDismissListener { dismissed ->
                if (performanceDialog === dismissed) performanceDialog = null
            }
        }
    }

    private fun modeInfoCard(): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; tag = "mode_tip"
        setPadding(0, ui.dp(14), 0, 0)
        addView(View(this@MainActivity).apply {
            setBackgroundColor(if (ui.dark) 0xFF33333A.toInt() else 0xFFE4E8EF.toInt())
        }, LinearLayout.LayoutParams(-1, ui.dp(1)))
        modeDescription = ui.text("", 14f, color = ui.primary).apply {
            setLineSpacing(ui.dp(3).toFloat(), 1f); setPadding(0, ui.dp(12), 0, 0)
        }
        addView(modeDescription)
    }

    private fun modeButton(mode: AudioModes.Mode): LinearLayout = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER
        setPadding(ui.dp(3), ui.dp(12), ui.dp(3), ui.dp(12)); minimumHeight = ui.dp(92)
        isClickable = true; isFocusable = true; contentDescription = getString(mode.label); tag = "mode_${mode.id}"
        accessibilityDelegate = object : View.AccessibilityDelegate() {
            override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                super.onInitializeAccessibilityNodeInfo(host, info)
                info.className = "android.widget.RadioButton"; info.isCheckable = true; info.isChecked = host.isSelected
            }
        }
        val icon = ImageView(this@MainActivity).apply { setImageResource(mode.icon); importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO }
        addView(icon, LinearLayout.LayoutParams(ui.dp(24), ui.dp(24)))
        val label = ui.text(mode.label, 12f, true).apply {
            gravity = Gravity.CENTER; setPadding(0, ui.dp(9), 0, 0); importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }
        addView(label); buttons[mode.id] = this; icons[mode.id] = icon; labels[mode.id] = label
        setOnClickListener { selectMode(mode.id) }
    }

    private fun selectMode(mode: Int) {
        if (CleanerPrefs.status(this) == "Stopping") return

        if (mode == AudioModes.OFF) PlaybackController.disable(this)
        else {
            if (!ready()) { startActivity(Intent(this, SetupActivity::class.java).putExtra("page", 1)); return }
            CleanerPrefs.setSourcePreset(this, mode)
            PlaybackController.enable(this)
        }
        refresh()
    }
    private fun ready(): Boolean = when (ShizukuBackend.availabilityState()) {
        ShizukuBackend.Companion.Availability.READY_SHELL, ShizukuBackend.Companion.Availability.READY_ROOT,
        ShizukuBackend.Companion.Availability.READY_OTHER -> true
        else -> false
    }

    private fun refresh() {
        if (!::statusText.isInitialized) return
        CleanerPrefs.reconcileRuntimeState(this)
        val enabled = CleanerPrefs.isEnabled(this); val status = CleanerPrefs.status(this); val detail = CleanerPrefs.error(this)
        val availability = ShizukuBackend.availabilityState()
        val shizukuReady = ready()
        val recovering = enabled && detail.startsWith("AI temporarily bypassed")
        val recent = enabled && CleanerPrefs.recoveryUntil(this) > SystemClock.elapsedRealtime()
        val stopping = status == "Stopping"
        val failed = !enabled && detail.isNotBlank() && status != "Off"
        val label = when {
            !shizukuReady -> if (availability == ShizukuBackend.Companion.Availability.PERMISSION_NEEDED) R.string.shizuku_permission_needed else R.string.shizuku_not_running
            failed -> R.string.audio_issue
            stopping -> R.string.stopping
            enabled && status == "Starting privileged audio route" -> R.string.starting
            recovering -> R.string.recovering
            recent -> R.string.recently_recovered
            enabled -> R.string.active
            else -> R.string.off
        }
        val color = when {
            failed -> Color.rgb(235, 105, 105)
            !shizukuReady || recovering || recent || stopping || status == "Starting privileged audio route" -> ui.warning
            enabled -> Color.rgb(65, 184, 127)
            else -> ui.secondary
        }
        statusText.setText(label)
        if (lastStatusColor != color) { lastStatusColor = color; statusText.setTextColor(color); statusDot.background = ui.circle(color) }
        statusDetail.text = when {
            !shizukuReady -> getString(R.string.status_shizuku_help)
            failed -> detail
            recovering && CleanerPrefs.silentRecovery(this) -> getString(R.string.recovery_silent)
            recovering -> getString(R.string.recovery_original)
            recent -> getString(R.string.recovery_recent_detail)
            enabled -> ""
            else -> ""
        }
        statusDetail.visibility = if (statusDetail.text.isBlank()) View.GONE else View.VISIBLE
        statusAction.visibility = if (!shizukuReady || failed) View.VISIBLE else View.GONE
        if (::qualityValue.isInitialized) {
            qualityValue.text = getString(EnginePerformance.modes[EnginePerformance.selected(this)].label)
            (qualityValue.parent as View).contentDescription = getString(R.string.engine_performance) + ", " + qualityValue.text
        }
        bluetoothNotice.visibility = if (enabled && CleanerPrefs.bluetoothOutput(this)) View.VISIBLE else View.GONE
        overloadTip.visibility = if (enabled && CleanerPrefs.overloadTip(this) && CleanerPrefs.buffering(this) < 2) View.VISIBLE else View.GONE
        bufferingAction.setText(if (CleanerPrefs.buffering(this) == 0) R.string.use_balanced else R.string.use_maximum)
        (bufferingAction.parent as View).contentDescription = bufferingAction.text
        val selected = if (enabled) CleanerPrefs.sourcePreset(this) else AudioModes.OFF
        backgroundPanel.visibility = if (selected == CleanerPrefs.SOURCE_OPTIMIZED) View.VISIBLE else View.GONE
        if (!trackingBackground) {
            val value = CleanerPrefs.optimizedBackground(this)
            backgroundSlider.progress = value + 20
            backgroundValue.text = "${if (value > 0) "+" else ""}$value dB"
        }
        val signature = "$selected:$stopping"
        if (signature != selectionSignature) {
            selectionSignature = signature
            AudioModes.modes.forEach { mode ->
                val checked = selected == mode.id
                buttons[mode.id]?.apply {
                    isSelected = checked; isEnabled = !stopping; alpha = if (stopping) .55f else 1f
                    val base = if (checked) tint(mode.color) else if (ui.dark) Color.rgb(39, 39, 44) else Color.rgb(245, 245, 248)
                    background = RippleDrawable(ColorStateList.valueOf(Color.argb(45, Color.red(mode.color), Color.green(mode.color), Color.blue(mode.color))),
                        ui.rounded(base, 16).apply { setStroke(ui.dp(if (checked) 2 else 1), if (checked) mode.color else Color.TRANSPARENT) }, ui.rounded(Color.WHITE, 16))
                }
                icons[mode.id]?.imageTintList = ColorStateList.valueOf(if (checked) mode.color else ui.secondary)
                labels[mode.id]?.setTextColor(if (checked) mode.color else ui.secondary)
            }
        }
        val details = "$selected:${CleanerPrefs.optimizedBackground(this)}"
        if (details != detailSignature) {
            detailSignature = details
            val mode = AudioModes.mode(selected)

            modeDescription.text = TextAccents.color(this, getString(mode.description))
        }
        handler.removeCallbacks(expiry)
        if (recent && !recovering) handler.postDelayed(expiry, (CleanerPrefs.recoveryUntil(this) - SystemClock.elapsedRealtime()).coerceAtLeast(1) + 10)
    }
    private fun tint(color: Int): Int {
        val base = ui.cardColor
        return Color.rgb((Color.red(base) * 4 + Color.red(color)) / 5, (Color.green(base) * 4 + Color.green(color)) / 5, (Color.blue(base) * 4 + Color.blue(color)) / 5)
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
