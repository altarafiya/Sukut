package com.osama.cleanwave

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.widget.ImageView
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import android.os.Build
import rikka.shizuku.Shizuku

/** An Android-specific guide: Shizuku and media volume, never desktop VB-CABLE. */
class SetupActivity : Activity() {
    private lateinit var ui: MobileUi
    private lateinit var pages: FrameLayout
    private var page = 0
    private var appearance = ""
    private var connectionStatus: TextView? = null
    private var nextButton: View? = null
    private var permissionButton: View? = null
    private var debuggingButton: View? = null
    private var backCallback: OnBackInvokedCallback? = null
    private val received = Shizuku.OnBinderReceivedListener { refreshConnection() }
    private val dead = Shizuku.OnBinderDeadListener { refreshConnection() }
    private val permission = Shizuku.OnRequestPermissionResultListener { _, _ -> refreshConnection() }
    override fun attachBaseContext(base: Context) { super.attachBaseContext(UiPrefs.wrapLocale(base)) }
    override fun onCreate(state: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(state)
        page = (state?.getInt("page") ?: intent.getIntExtra("page", 0)).coerceIn(0, 3)
        appearance = UiPrefs.signature(this)
        ui = MobileUi(this); ui.styleBars()
        pages = FrameLayout(this)
        pages.setBackgroundColor(ui.background)
        setContentView(pages)
        render()
        if (Build.VERSION.SDK_INT >= 33) backCallback = OnBackInvokedCallback { back() }.also {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, it)
        }
        Shizuku.addBinderReceivedListenerSticky(received); Shizuku.addBinderDeadListener(dead)
        Shizuku.addRequestPermissionResultListener(permission)
    }
    override fun onSaveInstanceState(state: Bundle) { state.putInt("page", page); super.onSaveInstanceState(state) }
    override fun onResume() {
        super.onResume()
        if (appearance != UiPrefs.signature(this)) { recreate(); return }
        refreshConnection()
    }
    override fun onDestroy() {
        if (Build.VERSION.SDK_INT >= 33) backCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        Shizuku.removeBinderReceivedListener(received); Shizuku.removeBinderDeadListener(dead)
        Shizuku.removeRequestPermissionResultListener(permission); super.onDestroy()
    }
    @Suppress("DEPRECATION") override fun onBackPressed() = back()
    private fun back() { if (page > 0) { page--; render(-1) } else finish() }
    private fun render(direction: Int = 0) {
        connectionStatus = null; nextButton = null; permissionButton = null; debuggingButton = null
        val screen = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setOnApplyWindowInsetsListener { view, insets ->
                if (Build.VERSION.SDK_INT >= 30) {
                    val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                    view.setPadding(bars.left, bars.top, bars.right, bars.bottom + ui.dp(12))
                } else {
                    @Suppress("DEPRECATION")
                    view.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                        insets.systemWindowInsetRight, insets.systemWindowInsetBottom + ui.dp(12))
                }
                insets
            }
        }
        val root = AdaptiveColumn(this, ui.dp(760)).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(ui.dp(22), ui.dp(12), ui.dp(22), ui.dp(16))
        }
        root.addView(ui.text(getString(R.string.setup_step, page + 1, 4), 13f, color = ui.secondary))
        root.addView(ui.space(16))
        val card = ui.card(22)
        val title = intArrayOf(R.string.setup_welcome, R.string.setup_shizuku_title, R.string.setup_sound_title, R.string.setup_ready_title)[page]
        val body = intArrayOf(R.string.setup_welcome_body, R.string.setup_shizuku_body, R.string.setup_sound_body, R.string.setup_ready_body)[page]
        val icon = intArrayOf(R.drawable.ic_music_filter, R.drawable.ic_system_theme, R.drawable.ic_sliders, R.drawable.ic_check_circle)[page]
        val hero = ImageView(this).apply {
            setImageResource(if (page == 0) R.drawable.sukut_mark else icon)
            if (page != 0) imageTintList = android.content.res.ColorStateList.valueOf(ui.accent)
        }
        val heroSize = ui.dp(if (page == 0) 88 else 42)
        card.addView(hero, LinearLayout.LayoutParams(heroSize, heroSize))
        card.addView(ui.space(14)); card.addView(ui.text(TextAccents.color(this, getString(title)), 24f, true).apply {
            isAccessibilityHeading = true; includeFontPadding = true
            textDirection = if (resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL) View.TEXT_DIRECTION_RTL else View.TEXT_DIRECTION_LTR
            textAlignment = View.TEXT_ALIGNMENT_GRAVITY
            gravity = (if (resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL) Gravity.RIGHT else Gravity.LEFT) or Gravity.TOP
            setPaddingRelative(ui.dp(6), ui.dp(4), ui.dp(6), ui.dp(4))
        }, LinearLayout.LayoutParams(-1, -2))
        if (page != 2) { card.addView(ui.space(10)); card.addView(ui.text(TextAccents.color(this, getString(body)), 15f, color = ui.secondary)) }
        when (page) {
            0 -> {
                card.addView(ui.space(24)); card.addView(ui.text(R.string.setup_language, 15f, true))
                card.addView(ui.space(10))
                card.addView(ui.button(R.string.english, R.drawable.ic_language) { UiPrefs.setLanguage(this, "en"); recreate() })
                card.addView(ui.space(8))
                card.addView(ui.button(R.string.arabic, R.drawable.ic_language) { UiPrefs.setLanguage(this, "ar"); recreate() })
            }
            1 -> {
                card.addView(ui.space(16))
                connectionStatus = ui.text("", 14f, true, ui.warning).also { card.addView(it) }
                ShizukuGuide.add(this, card)
                card.addView(ui.space(8))
                permissionButton = ui.button(R.string.setup_permission, R.drawable.ic_check_circle) {
                    if (runCatching { Shizuku.pingBinder() }.getOrDefault(false)) {
                        runCatching { Shizuku.requestPermission(201) }
                    } else AudioTools.openShizuku(this)
                }
                card.addView(permissionButton)
                debuggingButton = null
                card.addView(ui.space(14)); card.addView(ui.text(TextAccents.color(this, getString(R.string.setup_shizuku_hint)), 13f, color = ui.secondary))
            }
            2 -> {
                card.addView(ui.space(12)); card.addView(ui.text(TextAccents.color(this, getString(R.string.performance_help)), 13f, color = ui.secondary))
                card.addView(ui.space(12)); card.addView(EnginePerformance.choices(this))
            }
            3 -> {
                AudioModes.modes.forEach { mode ->
                    card.addView(ui.space(18))
                    card.addView(LinearLayout(this).apply {
                        gravity = Gravity.CENTER_VERTICAL
                        addView(ImageView(this@SetupActivity).apply {
                            setImageResource(mode.icon); imageTintList = android.content.res.ColorStateList.valueOf(mode.color)
                        }, LinearLayout.LayoutParams(ui.dp(24), ui.dp(24)).apply { marginEnd = ui.dp(10) })
                        addView(ui.text(mode.label, 16f, true, mode.color), LinearLayout.LayoutParams(0, -2, 1f))
                    })
                    card.addView(ui.text(TextAccents.color(this, getString(mode.description)), 14f, color = ui.secondary).apply { setPadding(0, ui.dp(5), 0, 0) })
                }
                card.addView(ui.space(16)); card.addView(ui.text(R.string.setup_model_note, 12f, color = ui.secondary))
                card.addView(ui.space(22))
                card.addView(ui.text("ادعوا لخالي بالرَّحمة، ولزوجته بالشِّفاء،\nولجميع المسلمين.", 17f, color = ui.warning).apply {
                    textDirection = View.TEXT_DIRECTION_RTL
                    textAlignment = View.TEXT_ALIGNMENT_CENTER
                    gravity = Gravity.CENTER
                    includeFontPadding = true
                    setPaddingRelative(ui.dp(8), ui.dp(10), ui.dp(8), ui.dp(10))
                }, LinearLayout.LayoutParams(-1, -2))
            }
        }
        root.addView(card); root.addView(ui.space(16))
        screen.addView(ui.scroll(root), LinearLayout.LayoutParams(-1, 0, 1f))
        val navigation = AdaptiveColumn(this, ui.dp(760)).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(ui.dp(22), ui.dp(12), ui.dp(22), ui.dp(12))
            if (page > 0) addView(ui.button(R.string.back) { back() }, LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = ui.dp(8) })
            nextButton = ui.button(if (page == 3) R.string.finish_setup else R.string.next) {
                if (page == 3) {
                    UiPrefs.finishSetup(this@SetupActivity)
                    startActivity(Intent(this@SetupActivity, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP))
                    finish()
                } else { page++; render(1) }
            }
            addView(nextButton, LinearLayout.LayoutParams(0, -2, 1f))
        }
        screen.addView(FrameLayout(this).apply {
            addView(navigation, FrameLayout.LayoutParams(-1, -2, Gravity.CENTER_HORIZONTAL))
        }, LinearLayout.LayoutParams(-1, -2))
        SukutTypography.apply(screen)
        StartupMotion.slidePage(pages, screen, direction)
        // New animated pages must receive insets too, not only the first page.
        pages.rootWindowInsets?.let { screen.dispatchApplyWindowInsets(it) }
        screen.requestApplyInsets()
        refreshConnection()
    }
    private fun refreshConnection() {
        if (page != 1) return
        val available = ShizukuBackend.availabilityState()
        val ready = available == ShizukuBackend.Companion.Availability.READY_SHELL || available == ShizukuBackend.Companion.Availability.READY_ROOT || available == ShizukuBackend.Companion.Availability.READY_OTHER
        connectionStatus?.text = getString(if (ready) R.string.shizuku_ready_short else if (available == ShizukuBackend.Companion.Availability.PERMISSION_NEEDED) R.string.shizuku_permission_needed else R.string.setup_shizuku_required)
        connectionStatus?.setTextColor(if (ready) Color.rgb(65, 184, 127) else ui.warning)
        nextButton?.isEnabled = ready; nextButton?.alpha = if (ready) 1f else .45f
        permissionButton?.visibility = if (ready) View.GONE else View.VISIBLE
        debuggingButton?.visibility = if (ready) View.GONE else View.VISIBLE
    }
}
