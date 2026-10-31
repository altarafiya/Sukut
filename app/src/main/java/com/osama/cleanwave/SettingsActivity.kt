package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.LinearGradient
import android.graphics.Shader
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView

/**
 * Settings: every choice is visible and changes in one tap. Appearance and
 * language are inline segmented controls instead of pickers in a sheet.
 */
class SettingsActivity : Activity() {

    private var tintDialog: Dialog? = null
    private lateinit var ui: MobileUi
    private lateinit var workerStatus: TextView
    private val audioHandler = Handler(Looper.getMainLooper())
    private var amplificationDb = 0
    private var trackingAmplification = false
    private var pendingAmplification = false
    private val commitAmplification = Runnable {
        pendingAmplification = false
        CleanerPrefs.setAmplification(this, amplificationDb)
        reconfigureAudio()
    }

    private val binderReceivedListener = WorkerConnection.OnChanged { refreshWorker() }
    private val binderDeadListener = WorkerConnection.OnChanged { refreshWorker() }

    override fun attachBaseContext(newBase: Context) { super.attachBaseContext(UiPrefs.wrapLocale(newBase)) }

    override fun onCreate(savedInstanceState: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(savedInstanceState)
        AppNavigation.configureSettings(this)
        amplificationDb = CleanerPrefs.amplification(this)
        ui = MobileUi(this); ui.styleBars()
        setContentView(buildUi())
        AppNavigation.handleBack(this) { returnHome() }
        WorkerConnection.addListener(binderReceivedListener)
        WorkerConnection.addListener(binderDeadListener)
        
    }

    @Deprecated("Android 12 and earlier; newer versions use AppNavigation.handleBack")
    @Suppress("DEPRECATION")
    override fun onBackPressed() = returnHome()

    override fun onResume() { super.onResume(); refreshWorker() }

    override fun onPause() {
        audioHandler.removeCallbacks(commitAmplification)
        if (pendingAmplification) commitAmplification.run()
        super.onPause()
    }

    override fun onDestroy() {
        tintDialog?.dismiss(); tintDialog = null
        audioHandler.removeCallbacks(commitAmplification)
        if (pendingAmplification) commitAmplification.run()
        WorkerConnection.removeListener(binderReceivedListener)
        WorkerConnection.removeListener(binderDeadListener)
        
        super.onDestroy()
    }

    private fun returnHome() = AppNavigation.closeSettings(this)

    private fun buildUi(): ScrollView {
        val root = ui.root(680)
        root.addView(LinearLayout(this).apply {
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, ui.dp(8), 0, 0)
            addView(ui.iconButton(R.drawable.ic_back, R.string.back) { returnHome() }, LinearLayout.LayoutParams(ui.dp(48), ui.dp(48)))
            addView(ui.title(R.string.settings).apply { setPaddingRelative(ui.dp(14), 0, 0, 0) }, LinearLayout.LayoutParams(0, -2, 1f))
        })
        root.addView(ui.space(24))

        root.addView(ui.sectionLabel(R.string.settings_general))
        root.addView(ui.card(16).apply {
            addView(label(R.drawable.ic_system_theme, R.string.appearance))
            addView(SegmentedChoice(this@SettingsActivity,
                listOf(getString(R.string.system_default), getString(R.string.light), getString(R.string.dark)),
                UiPrefs.theme(this@SettingsActivity)) { which ->
                UiPrefs.setTheme(this@SettingsActivity, which); recreate()
            }.apply { tag = "appearance_choice" }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
            addView(ui.space(20)); addView(backgroundPreferences())
            addView(ui.space(20))
            addView(label(R.drawable.ic_language, R.string.language))
            val languages = arrayOf(UiPrefs.LANGUAGE_SYSTEM, UiPrefs.LANGUAGE_ENGLISH, UiPrefs.LANGUAGE_ARABIC)
            addView(SegmentedChoice(this@SettingsActivity,
                // Language names are always written in their own language.
                listOf(getString(R.string.system_default), getString(R.string.english_native), getString(R.string.arabic_native)),
                languages.indexOf(UiPrefs.language(this@SettingsActivity)).coerceAtLeast(0)) { which ->
                UiPrefs.setLanguage(this@SettingsActivity, languages[which]); recreate()
            }.apply { tag = "language_choice" }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
        })

        root.addView(ui.space(24))
        root.addView(ui.sectionLabel(R.string.audio_settings))
        root.addView(audioPreferences())

        root.addView(ui.space(24))
        root.addView(ui.sectionLabel(R.string.settings_connection))
        workerStatus = ui.valueText("")
        root.addView(ui.group().apply {
            addView(ui.row(R.drawable.ic_sukut_tile, getString(R.string.worker), workerStatus) {
                AppNavigation.openActivation(this@SettingsActivity)
            }.apply { tag = "settings_audio_worker" })
        })

        root.addView(ui.space(24))
        root.addView(ui.sectionLabel(R.string.settings_help))
        root.addView(ui.group().apply {
            addView(ui.row(R.drawable.ic_setup, getString(R.string.run_setup)) {
                AppNavigation.openSetup(this@SettingsActivity)
            })
            addView(ui.row(R.drawable.ic_lock,getString(R.string.privacy_policy)) {
                AudioTools.website(this@SettingsActivity,ReleaseLinks.PRIVACY_POLICY)
            })
            addView(ui.row(R.drawable.ic_info,getString(R.string.support_email)) { AudioTools.support(this@SettingsActivity) })
        })

        root.addView(ui.space(24))
        root.addView(ui.sectionLabel(R.string.about))
        root.addView(about())
        return ui.scroll(root).apply { id = R.id.settings_scroll; tag = "settings_scroll" }
    }

    private fun label(icon: Int, title: Int) = LinearLayout(this).apply {
        gravity = Gravity.CENTER_VERTICAL
        addView(ui.badge(icon), LinearLayout.LayoutParams(ui.dp(30), ui.dp(30)).apply { marginEnd = ui.dp(14) })
        addView(ui.text(title, 16f, true), LinearLayout.LayoutParams(0, -2, 1f))
    }

    private fun backgroundPreferences(): View = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        addView(label(R.drawable.ic_palette, R.string.background_style))
        addView(SegmentedChoice(this@SettingsActivity,
            listOf(getString(R.string.background_solid), getString(R.string.background_tinted)),
            if (UiPrefs.tinted(this@SettingsActivity)) 1 else 0) { which ->
            UiPrefs.setTinted(this@SettingsActivity, which == 1); recreate()
        }.apply { tag = "background_style" }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
        if (UiPrefs.tinted(this@SettingsActivity)) addView(LinearLayout(this@SettingsActivity).apply {
            tag = "tint_color_option"; gravity = Gravity.CENTER_VERTICAL
            background = ui.pressable(ui.palette.cardAlt, 12); minimumHeight = ui.dp(56)
            setPadding(ui.dp(12), ui.dp(8), ui.dp(12), ui.dp(8)); isFocusable = true
            contentDescription = getString(R.string.tint_color) + ", " + ThemeColors.hex(UiPrefs.tintColor(this@SettingsActivity))
            addView(View(this@SettingsActivity).apply { background = ui.circle(UiPrefs.tintColor(this@SettingsActivity)).apply { setStroke(ui.dp(1), ui.dividerColor) } }, LinearLayout.LayoutParams(ui.dp(26), ui.dp(26)).apply { marginEnd = ui.dp(12) })
            addView(ui.text(R.string.tint_color, 15f), LinearLayout.LayoutParams(0, -2, 1f))
            addView(ui.text(ThemeColors.hex(UiPrefs.tintColor(this@SettingsActivity)), 13f, color = ui.secondary).apply { textDirection = View.TEXT_DIRECTION_LTR })
            setOnClickListener {
                tintDialog?.dismiss()
                tintDialog = TintColorPicker.show(this@SettingsActivity, UiPrefs.tintColor(this@SettingsActivity)) { color ->
                    UiPrefs.setTintColor(this@SettingsActivity, color); recreate()
                }.also { dialog -> dialog.setOnDismissListener { if (tintDialog === dialog) tintDialog = null } }
            }
        }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(10) })
    }

    private fun reconfigureAudio() {
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(this))
            startService(Intent(this, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
    }

    private fun audioPreferences(): View = ui.card(16).apply {
        val gain = ui.text("+$amplificationDb dB", 15f, true, ui.accent).apply { textDirection = View.TEXT_DIRECTION_LTR }
        addView(LinearLayout(this@SettingsActivity).apply {
            gravity = Gravity.CENTER_VERTICAL
            addView(label(R.drawable.ic_volume, R.string.amplification), LinearLayout.LayoutParams(0, -2, 1f))
            addView(gain)
        })
        addView(TouchSeekBar(this@SettingsActivity).apply {
            max = 12; progress = amplificationDb
            contentDescription = getString(R.string.amplification)
            SliderStyle.apply(this, ui)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    amplificationDb = progress; gain.text = "+$progress dB"; pendingAmplification = true
                    // Keyboard/accessibility steps commit shortly; drags commit on release.
                    if (!trackingAmplification) { audioHandler.removeCallbacks(commitAmplification); audioHandler.postDelayed(commitAmplification, 90) }
                }
                override fun onStartTrackingTouch(bar: SeekBar?) { trackingAmplification = true }
                override fun onStopTrackingTouch(bar: SeekBar?) {
                    bar?.performHapticFeedback(android.view.HapticFeedbackConstants.CLOCK_TICK)
                    trackingAmplification = false
                    audioHandler.removeCallbacks(commitAmplification); if (pendingAmplification) commitAmplification.run()
                }
            })
        }, LinearLayout.LayoutParams(-1, ui.dp(64)).apply { topMargin = ui.dp(8) })
        addView(ui.text(R.string.amplification_hint, 13f, color = ui.secondary).apply { setPaddingRelative(ui.dp(44), ui.dp(4), 0, 0) })
    }

    private fun about(): View = ui.card(20).apply {
        gravity = Gravity.CENTER_HORIZONTAL
        addView(ui.brandIcon(64), LinearLayout.LayoutParams(ui.dp(64), ui.dp(64)))
        addView(centered(ui.text(R.string.app_name, 22f, true)).apply { setPadding(0, ui.dp(10), 0, 0) }, LinearLayout.LayoutParams(-1, -2))
        addView(centered(ui.text(R.string.about_tagline, 14f, color = ui.secondary)), LinearLayout.LayoutParams(-1, -2))
        val version = android.text.BidiFormatter.getInstance(ui.rtl).unicodeWrap(BuildConfig.VERSION_NAME, android.text.TextDirectionHeuristics.LTR)
        addView(centered(ui.text(getString(R.string.version_value, version), 13f, color = ui.secondary)).apply {
            textDirection = View.TEXT_DIRECTION_LOCALE; setPadding(0, ui.dp(6), 0, 0)
        }, LinearLayout.LayoutParams(-1, -2))
        addView(centered(ui.text(R.string.developed_by, 13f, color = ui.secondary)).apply { setPadding(0, ui.dp(16), 0, 0) }, LinearLayout.LayoutParams(-1, -2))
        addView(RainbowBrandView(this@SettingsActivity), LinearLayout.LayoutParams(-1, ui.dp(64)))
        addView(ui.space(12)); addView(ui.rule(), LinearLayout.LayoutParams(-1, ui.dp(1)))
        val notes = LinearLayout(this@SettingsActivity).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
        val toggle = ui.row(R.drawable.ic_info, getString(R.string.changelog), null, chevron = true) {
            val show = notes.visibility != View.VISIBLE
            // Built on demand and released when closed.
            if (show) notes.addView(ui.text(getString(R.string.sukut_changelog_latest), 14f, color = ui.secondary).apply {
                setLineSpacing(ui.dp(4).toFloat(), 1f); setPadding(ui.dp(4), 0, ui.dp(4), ui.dp(8))
            }) else notes.removeAllViews()
            notes.visibility = if (show) View.VISIBLE else View.GONE
        }.apply { background = ui.pressable(ui.cardColor, 16); setPaddingRelative(ui.dp(4), ui.dp(12), ui.dp(4), ui.dp(12)) }
        addView(toggle, LinearLayout.LayoutParams(-1, -2))
        addView(notes, LinearLayout.LayoutParams(-1, -2))
    }

    private fun centered(view: TextView) = view.apply { gravity = Gravity.CENTER; textAlignment = View.TEXT_ALIGNMENT_CENTER }

    private fun refreshWorker() {
        if (!::workerStatus.isInitialized) return
        val ready = WorkerConnection.ready
        workerStatus.setText(if (ready) R.string.worker_ready_short else R.string.worker_not_running)
        workerStatus.setTextColor(if (ready) ui.accent else ui.warning)
    }
}
