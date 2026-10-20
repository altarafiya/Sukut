package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.ColorDrawable
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.Window
import android.view.WindowManager
import android.view.accessibility.AccessibilityNodeInfo
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.SeekBar
import rikka.shizuku.Shizuku

class SettingsActivity : Activity() {
    companion object {
        private const val REQ_SHIZUKU = 201
        private const val SHIZUKU_PACKAGE = "moe.shizuku.privileged.api"
    }

    private val palette by lazy { UiPalette(this) }
    private val dark get() = palette.dark
    private val backgroundColor get() = palette.background
    private val cardColor get() = palette.card
    private val primaryText get() = palette.primary
    private val secondaryText get() = palette.secondary
    private val accent = Color.rgb(52, 130, 255)
    private val warning = Color.rgb(255, 159, 10)

    private lateinit var appearanceValue: TextView
    private lateinit var languageValue: TextView
    private lateinit var shizukuStatus: TextView
    private lateinit var shizukuAction: TextView
    private var choiceDialog: Dialog? = null
    private var backCallback: OnBackInvokedCallback? = null
    private val audioHandler = Handler(Looper.getMainLooper())
    private var amplificationDb = 0
    private var trackingAmplification = false
    private var pendingAmplification = false
    private val commitAmplification = Runnable {
        pendingAmplification = false
        CleanerPrefs.setAmplification(this, amplificationDb)
        reconfigureAudio()
    }

    private val binderReceivedListener = Shizuku.OnBinderReceivedListener { refreshShizuku() }
    private val binderDeadListener = Shizuku.OnBinderDeadListener { refreshShizuku() }
    private val permissionListener = Shizuku.OnRequestPermissionResultListener { code, _ ->
        if (code == REQ_SHIZUKU) refreshShizuku()
    }

    override fun attachBaseContext(newBase: Context) {
        super.attachBaseContext(UiPrefs.wrapLocale(newBase))
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        setTheme(if (UiPrefs.isDark(this)) R.style.Theme_CleanWave_Dark else R.style.Theme_CleanWave_Light)
        super.onCreate(savedInstanceState)
        AppNavigation.configureSettings(this)
        amplificationDb = CleanerPrefs.amplification(this)
        styleSystemBars()
        setContentView(buildUi())
        if (Build.VERSION.SDK_INT >= 33) {
            backCallback = OnBackInvokedCallback { returnHome() }.also {
                onBackInvokedDispatcher.registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, it)
            }
        }
        Shizuku.addBinderReceivedListenerSticky(binderReceivedListener)
        Shizuku.addBinderDeadListener(binderDeadListener)
        Shizuku.addRequestPermissionResultListener(permissionListener)
    }

    override fun onResume() {
        super.onResume()
        refreshShizuku()
    }

    override fun onDestroy() {
        audioHandler.removeCallbacks(commitAmplification)
        if (pendingAmplification) commitAmplification.run()
        choiceDialog?.dismiss()
        if (Build.VERSION.SDK_INT >= 33) backCallback?.let { onBackInvokedDispatcher.unregisterOnBackInvokedCallback(it) }
        Shizuku.removeBinderReceivedListener(binderReceivedListener)
        Shizuku.removeBinderDeadListener(binderDeadListener)
        Shizuku.removeRequestPermissionResultListener(permissionListener)
        super.onDestroy()
    }

    override fun onPause() {
        audioHandler.removeCallbacks(commitAmplification)
        if (pendingAmplification) commitAmplification.run()
        super.onPause()
    }

    @Suppress("DEPRECATION")
    override fun onBackPressed() = returnHome()

    private fun returnHome() {
        AppNavigation.closeSettings(this)
    }

    private fun buildUi(): ScrollView {
        val root = AdaptiveColumn(this, dp(760)).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(8), dp(22), dp(30))
            setBackgroundColor(backgroundColor)
            setOnApplyWindowInsetsListener { view, insets ->
                val top = if (Build.VERSION.SDK_INT >= 30) {
                    insets.getInsets(WindowInsets.Type.systemBars()).top
                } else {
                    @Suppress("DEPRECATION") insets.systemWindowInsetTop
                }
                val bottom = if (Build.VERSION.SDK_INT >= 30) {
                    insets.getInsets(WindowInsets.Type.systemBars()).bottom
                } else {
                    @Suppress("DEPRECATION") insets.systemWindowInsetBottom
                }
                view.setPadding(dp(22), dp(8) + top, dp(22), dp(30) + bottom)
                insets
            }
        }

        root.addView(LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(ImageButton(this@SettingsActivity).apply {
                setImageResource(R.drawable.ic_back)
                imageTintList = ColorStateList.valueOf(primaryText)
                background = circle(cardColor)
                contentDescription = getString(R.string.back)
                setPadding(dp(10), dp(10), dp(10), dp(10))
                setOnClickListener { returnHome() }
            }, LinearLayout.LayoutParams(dp(42), dp(42)))
            addView(TextView(this@SettingsActivity).apply {
                text = getString(R.string.settings)
                textSize = 25f
                setTextColor(primaryText)
                setTypeface(typeface, Typeface.BOLD)
                setPaddingRelative(dp(14), 0, 0, 0)
            })
        })
        root.addView(space(18))

        root.addView(sectionTitle(getString(R.string.settings_general)))
        root.addView(settingsCard().apply {
            appearanceValue = valueText(themeLabel())
            addView(choiceRow(getString(R.string.appearance), appearanceValue) { showThemePicker() })
            addView(divider())
            languageValue = valueText(languageLabel())
            addView(choiceRow(getString(R.string.language), languageValue) { showLanguagePicker() })
        })

        root.addView(space(18))
        root.addView(sectionTitle(getString(R.string.audio_settings)))
        root.addView(audioPreferences())
        root.addView(space(18))
        root.addView(sectionTitle(getString(R.string.settings_connection)))
        root.addView(settingsCard().apply {
            addView(LinearLayout(this@SettingsActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(16), dp(14), dp(16), dp(14))
                val shizukuHeader = LinearLayout(this@SettingsActivity).apply { gravity = Gravity.CENTER_VERTICAL }
                shizukuHeader.addView(TextView(this@SettingsActivity).apply {
                    text = getString(R.string.shizuku)
                    setCompoundDrawablesRelativeWithIntrinsicBounds(R.drawable.ic_shizuku, 0, 0, 0)
                    compoundDrawablePadding = dp(10)
                    compoundDrawablesRelative[0]?.setTint(accent)
                    textSize = 16f
                    setTextColor(primaryText)
                    setTypeface(typeface, Typeface.BOLD)
                    setSingleLine(true)
                }, LinearLayout.LayoutParams(0, -2, 1f))
                shizukuStatus = TextView(this@SettingsActivity).apply {
                    textSize = 13f
                    setTextColor(secondaryText)
                    setPaddingRelative(dp(12), 0, 0, 0)
                    setSingleLine(true); ellipsize = android.text.TextUtils.TruncateAt.END
                    maxWidth = dp(190)
                }
                shizukuHeader.addView(shizukuStatus, LinearLayout.LayoutParams(-2, -2))
                addView(shizukuHeader, LinearLayout.LayoutParams(-1, -2))
                shizukuAction = TextView(this@SettingsActivity).apply {
                    text = getString(R.string.connect_shizuku)
                    textSize = 14f
                    setTextColor(accent)
                    setTypeface(typeface, Typeface.BOLD)
                    setPadding(0, dp(12), 0, dp(2))
                    setOnClickListener { handleShizukuAction() }
                    gravity = Gravity.CENTER
                    minHeight = dp(48)
                }
                addView(shizukuAction)
            })
        })

        root.addView(space(12))
        root.addView(space(18))
        root.addView(sectionTitle(getString(R.string.settings_help)))
        root.addView(settingsCard().apply {
            addView(choiceRow(getString(R.string.run_setup), valueText("›")) {
                startActivity(Intent(this@SettingsActivity, SetupActivity::class.java))
            })
        })
        root.addView(space(12))
        root.addView(collapsibleCard(R.string.about) { settingsCard().apply {
            addView(ImageView(this@SettingsActivity).apply {
                setImageResource(R.drawable.sukut_mark)
                setPadding(0, dp(16), 0, 0)
            }, LinearLayout.LayoutParams(dp(80), dp(96)).apply { gravity = Gravity.CENTER_HORIZONTAL })
            addView(TextView(this@SettingsActivity).apply {
                text = getString(R.string.app_name); textSize = 20f; gravity = Gravity.CENTER
                setTextColor(primaryText); setPadding(0, dp(10), 0, dp(6))
            })
            addView(infoRow(getString(R.string.version), BuildConfig.VERSION_NAME.substringBefore('-')))
            addView(TextView(this@SettingsActivity).apply {
                text = getString(R.string.developed_by)
                textSize = 14f; gravity = Gravity.CENTER; setTextColor(secondaryText)
                setPadding(dp(16), dp(4), dp(16), 0)
            })
            addView(TextView(this@SettingsActivity).apply {
                text = getString(R.string.developer_brand)
                textSize = 26f; gravity = Gravity.CENTER; setTypeface(typeface, Typeface.BOLD)
                setPadding(dp(16), dp(4), dp(16), dp(18))
                // Colour the already-shaped word, without a separate run per letter.
                addOnLayoutChangeListener { _, _, _, _, _, _, _, _, _ ->
                    val ink = paint.measureText(text.toString())
                    val left = (width - ink) / 2f
                    paint.shader = android.graphics.LinearGradient(left, 0f, left + maxOf(ink, 1f), 0f,
                        intArrayOf(0xFFE0A453.toInt(), 0xFF59CB83.toInt(), 0xFF48A9DB.toInt(), 0xFFAA81E8.toInt(), 0xFFE278A7.toInt()),
                        null, android.graphics.Shader.TileMode.CLAMP)
                    invalidate()
                }
            })
            addView(divider())
            addView(LinearLayout(this@SettingsActivity).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(dp(16), dp(14), dp(16), dp(16))
                addView(TextView(this@SettingsActivity).apply {
                    text = getString(R.string.changelog)
                    textSize = 15f
                    setTextColor(primaryText)
                    setTypeface(typeface, Typeface.BOLD)
                })
                addView(TextView(this@SettingsActivity).apply {
                    text = getString(R.string.sukut_changelog_latest) + "\n\n" + getString(R.string.sukut_changelog)
                    textSize = 13f
                    setTextColor(secondaryText)
                    setLineSpacing(dp(3).toFloat(), 1f)
                    setPadding(0, dp(8), 0, 0)
                })
            })
        } })

        SukutTypography.apply(root)
        return MobileUi(this).scroll(root)
    }

    private fun reconfigureAudio() {
        if (CleanerService.isAlive && CleanerPrefs.isEnabled(this))
            startService(Intent(this, CleanerService::class.java).setAction(CleanerService.ACTION_CONFIGURE))
    }
    private fun audioPreferences(): View = settingsCard().apply {
        val gain = valueText("+$amplificationDb dB").apply { textDirection = View.TEXT_DIRECTION_LTR }
        addView(choiceRow(getString(R.string.amplification), gain) { }.apply { isClickable = false; isFocusable = false })
        addView(SeekBar(this@SettingsActivity).apply {
            max = 12; progress = amplificationDb; minHeight = dp(48)
            contentDescription = getString(R.string.amplification)
            setPadding(dp(24), 0, dp(24), 0)
            progressTintList = ColorStateList.valueOf(accent); thumbTintList = ColorStateList.valueOf(accent)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, progress: Int, fromUser: Boolean) {
                    if (!fromUser) return
                    amplificationDb = progress; gain.text = "+$progress dB"; pendingAmplification = true
                    if (!trackingAmplification) { audioHandler.removeCallbacks(commitAmplification); audioHandler.postDelayed(commitAmplification, 90) }
                }
                override fun onStartTrackingTouch(bar: SeekBar?) { trackingAmplification = true }
                override fun onStopTrackingTouch(bar: SeekBar?) {
                    trackingAmplification = false
                    audioHandler.removeCallbacks(commitAmplification); if (pendingAmplification) commitAmplification.run()
                }
            })
        })
        addView(MobileUi(this@SettingsActivity).text(R.string.amplification_hint, 12f, color = secondaryText).apply { setPadding(dp(16), dp(4), dp(16), dp(16)) })
    }
    private fun collapsibleCard(title: Int, content: () -> View): View = settingsCard().apply {
        val body = LinearLayout(this@SettingsActivity).apply { orientation = LinearLayout.VERTICAL; visibility = View.GONE }
        val arrow = valueText("⌄")
        addView(choiceRow(getString(title), arrow) {
            val show = body.visibility != View.VISIBLE
            if (show && body.childCount == 0) body.addView(content())
            body.visibility = if (show) View.VISIBLE else View.GONE
            arrow.text = if (show) "⌃" else "⌄"
            if (show) SukutTypography.apply(body) else body.removeAllViews()
        })
        addView(body)
    }

    private fun showThemePicker() {
        showChoicePicker(
            title = R.string.appearance,
            subtitle = R.string.appearance_hint,
            options = listOf(
                PickerOption(R.string.system_default, R.string.theme_system_hint, R.drawable.ic_system_theme),
                PickerOption(R.string.light, R.string.theme_light_hint, R.drawable.ic_sun),
                PickerOption(R.string.dark, R.string.theme_dark_hint, R.drawable.ic_moon),
            ),
            selected = UiPrefs.theme(this),
        ) { which ->
            if (UiPrefs.theme(this) != which) {
                UiPrefs.setTheme(this, which)
                recreate()
            }
        }
    }

    private fun showLanguagePicker() {
        val values = arrayOf(UiPrefs.LANGUAGE_SYSTEM, UiPrefs.LANGUAGE_ENGLISH, UiPrefs.LANGUAGE_ARABIC)
        val selected = values.indexOf(UiPrefs.language(this)).coerceAtLeast(0)
        showChoicePicker(
            title = R.string.language,
            subtitle = R.string.language_hint,
            options = listOf(
                PickerOption(R.string.system_default, R.string.language_system_hint, R.drawable.ic_language),
                PickerOption(R.string.english, R.string.english_native, glyph = "Aa"),
                PickerOption(R.string.arabic, R.string.arabic_native, glyph = "ع"),
            ),
            selected = selected,
        ) { which ->
            if (UiPrefs.language(this) != values[which]) {
                UiPrefs.setLanguage(this, values[which])
                recreate()
            }
        }
    }

    private data class PickerOption(val label: Int, val detail: Int, val icon: Int = 0, val glyph: String = "")

    private fun showChoicePicker(title: Int, subtitle: Int, options: List<PickerOption>, selected: Int, onSelect: (Int) -> Unit) {
        choiceDialog?.dismiss()
        val dialog = Dialog(this)
        dialog.requestWindowFeature(Window.FEATURE_NO_TITLE)
        val content = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(24), dp(22), dp(16))
            background = rounded(cardColor, 28f)
        }
        content.addView(TextView(this).apply {
            text = getString(title)
            textSize = 22f
            setTextColor(primaryText)
            setTypeface(typeface, Typeface.BOLD)
            isAccessibilityHeading = true
        })
        content.addView(TextView(this).apply {
            text = getString(subtitle)
            textSize = 13f
            setTextColor(secondaryText)
            setPadding(0, dp(6), 0, dp(20))
        })
        options.forEachIndexed { index, option ->
            val checked = index == selected
            val surface = if (checked) {
                if (dark) Color.rgb(26, 48, 81) else Color.rgb(232, 241, 255)
            } else if (dark) Color.rgb(39, 39, 44) else Color.rgb(246, 246, 249)
            val shape = rounded(surface, 18f).apply { setStroke(dp(1), if (checked) accent else Color.TRANSPARENT) }
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                minimumHeight = dp(76)
                setPaddingRelative(dp(14), dp(14), dp(14), dp(14))
                background = RippleDrawable(ColorStateList.valueOf(Color.argb(35, 52, 130, 255)), shape, rounded(Color.WHITE, 18f))
                isClickable = true
                isFocusable = true
                isSelected = checked
                contentDescription = "${getString(option.label)}, ${getString(option.detail)}"
                accessibilityDelegate = object : View.AccessibilityDelegate() {
                    override fun onInitializeAccessibilityNodeInfo(host: View, info: AccessibilityNodeInfo) {
                        super.onInitializeAccessibilityNodeInfo(host, info)
                        info.className = "android.widget.RadioButton"
                        info.isCheckable = true
                        info.isChecked = checked
                    }
                }
                setOnClickListener {
                    dialog.dismiss()
                    onSelect(index)
                }
            }
            val iconColor = if (checked) accent else primaryText
            val badge: View = if (option.icon != 0) ImageView(this).apply {
                setImageResource(option.icon)
                imageTintList = ColorStateList.valueOf(iconColor)
                setPadding(dp(10), dp(10), dp(10), dp(10))
            } else TextView(this).apply {
                text = option.glyph
                textSize = if (option.glyph == "ع") 24f else 17f
                gravity = Gravity.CENTER
                setTextColor(iconColor)
                setTypeface(typeface, Typeface.BOLD)
            }
            badge.background = circle(if (dark) Color.rgb(54, 57, 66) else Color.WHITE)
            badge.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            row.addView(badge, LinearLayout.LayoutParams(dp(42), dp(42)).apply { marginEnd = dp(12) })
            row.addView(LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
                addView(TextView(this@SettingsActivity).apply {
                    text = getString(option.label)
                    textSize = 15f
                    setTextColor(if (checked) accent else primaryText)
                    setTypeface(typeface, Typeface.BOLD)
                })
                addView(TextView(this@SettingsActivity).apply {
                    text = getString(option.detail)
                    textSize = 12f
                    setTextColor(secondaryText)
                    setPadding(0, dp(4), 0, 0)
                })
            }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            row.addView(ImageView(this).apply {
                setImageResource(if (checked) R.drawable.ic_check_circle else R.drawable.ic_circle_outline)
                imageTintList = ColorStateList.valueOf(if (checked) accent else secondaryText)
                importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
            }, LinearLayout.LayoutParams(dp(22), dp(22)).apply { marginStart = dp(10) })
            content.addView(row, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply {
                if (index > 0) topMargin = dp(8)
            })
        }
        content.addView(TextView(this).apply {
            text = getString(R.string.cancel)
            textSize = 15f
            gravity = Gravity.CENTER
            setTextColor(secondaryText)
            minimumHeight = dp(48)
            isClickable = true
            isFocusable = true
            background = RippleDrawable(ColorStateList.valueOf(Color.argb(25, 52, 130, 255)), null, rounded(Color.WHITE, 14f))
            setOnClickListener { dialog.dismiss() }
        }, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(12) })
        SukutTypography.apply(content)
        dialog.setContentView(ScrollView(this).apply {
            overScrollMode = View.OVER_SCROLL_NEVER
            background = rounded(cardColor, 28f)
            clipToOutline = true
            addView(content)
        })
        dialog.setCanceledOnTouchOutside(true)
        dialog.window?.apply {
            setBackgroundDrawable(ColorDrawable(Color.TRANSPARENT))
            addFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND)
            setDimAmount(.45f)
            setGravity(if (resources.configuration.screenWidthDp >= 600) Gravity.CENTER else Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL)
        }
        choiceDialog = dialog
        dialog.setOnDismissListener { if (choiceDialog === dialog) choiceDialog = null }
        dialog.show()
        dialog.window?.apply {
            setLayout(minOf(dp(440), resources.displayMetrics.widthPixels - dp(32)), ViewGroup.LayoutParams.WRAP_CONTENT)
            attributes = attributes.apply { y = dp(20) }
        }
    }

    private fun refreshShizuku() {
        if (!::shizukuStatus.isInitialized) return
        val availability = ShizukuBackend.availabilityState()
        val ready = availability == ShizukuBackend.Companion.Availability.READY_SHELL ||
            availability == ShizukuBackend.Companion.Availability.READY_ROOT ||
            availability == ShizukuBackend.Companion.Availability.READY_OTHER
        shizukuStatus.text = when (availability) {
            ShizukuBackend.Companion.Availability.READY_SHELL -> getString(R.string.shizuku_running_shell)
            ShizukuBackend.Companion.Availability.READY_ROOT -> getString(R.string.shizuku_running_root)
            ShizukuBackend.Companion.Availability.READY_OTHER -> getString(R.string.connected)
            ShizukuBackend.Companion.Availability.PERMISSION_NEEDED -> getString(R.string.shizuku_permission_needed)
            ShizukuBackend.Companion.Availability.NOT_RUNNING -> getString(R.string.shizuku_not_running)
            ShizukuBackend.Companion.Availability.UNAVAILABLE -> getString(R.string.shizuku_unavailable)
        }
        shizukuStatus.setTextColor(if (ready) accent else warning)
        shizukuAction.visibility = if (ready) View.GONE else View.VISIBLE
        shizukuAction.text = when (availability) {
            ShizukuBackend.Companion.Availability.PERMISSION_NEEDED -> getString(R.string.grant_access)
            ShizukuBackend.Companion.Availability.NOT_RUNNING -> getString(R.string.open_shizuku)
            else -> getString(R.string.connect_shizuku)
        }
    }

    private fun handleShizukuAction() {
        runCatching {
            if (Shizuku.pingBinder()) {
                if (Shizuku.checkSelfPermission() != PackageManager.PERMISSION_GRANTED) {
                    Shizuku.requestPermission(REQ_SHIZUKU)
                }
            } else {
                packageManager.getLaunchIntentForPackage(SHIZUKU_PACKAGE)?.let { startActivity(it) }
            }
        }
        refreshShizuku()
    }

    private fun themeLabel(): String = when (UiPrefs.theme(this)) {
        UiPrefs.THEME_LIGHT -> getString(R.string.light)
        UiPrefs.THEME_DARK -> getString(R.string.dark)
        else -> getString(R.string.system_default)
    }

    private fun languageLabel(): String = when (UiPrefs.language(this)) {
        UiPrefs.LANGUAGE_ENGLISH -> getString(R.string.english)
        UiPrefs.LANGUAGE_ARABIC -> getString(R.string.arabic)
        else -> getString(R.string.system_default)
    }

    private fun settingsCard() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        background = rounded(cardColor, 22f)
    }

    private fun sectionTitle(text: String) = TextView(this).apply {
        this.text = text
        textSize = 13f
        setTextColor(secondaryText)
        setTypeface(typeface, Typeface.BOLD)
        setPaddingRelative(dp(6), 0, 0, dp(7))
    }

    private fun optionIcon(title: String) = when (title) {
        getString(R.string.appearance) -> R.drawable.ic_system_theme
        getString(R.string.language) -> R.drawable.ic_language
        getString(R.string.output_quality) -> R.drawable.ic_quality
        getString(R.string.buffering), getString(R.string.engine_performance) -> R.drawable.ic_buffer
        getString(R.string.amplification) -> R.drawable.ic_volume
        getString(R.string.audio_tools) -> R.drawable.ic_headphones
        getString(R.string.run_setup) -> R.drawable.ic_setup
        else -> R.drawable.ic_info
    }
    private fun optionColor(title: String) = when (title) {
        getString(R.string.appearance), getString(R.string.audio_tools) -> if (dark) 0xFFB7A2F5.toInt() else 0xFF8260BF.toInt()
        getString(R.string.buffering) -> if (dark) 0xFFE2BA77.toInt() else 0xFF9B722A.toInt()
        getString(R.string.amplification) -> if (dark) 0xFF66CE9A.toInt() else 0xFF2A8758.toInt()
        else -> accent
    }
    private fun optionBadge(title: String) = ImageView(this).apply {
        val color = optionColor(title)
        setImageResource(optionIcon(title)); imageTintList = ColorStateList.valueOf(color)
        background = circle(Color.argb(28, Color.red(color), Color.green(color), Color.blue(color)))
        setPadding(dp(7), dp(7), dp(7), dp(7))
        importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    private fun choiceRow(title: String, value: TextView, onClick: () -> Unit) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(16), dp(14), dp(16), dp(14))
        isClickable = true
        isFocusable = true
        setOnClickListener { onClick() }
        minimumHeight = dp(64)
        addView(optionBadge(title), LinearLayout.LayoutParams(dp(36), dp(36)).apply { marginEnd = dp(12) })
        addView(TextView(this@SettingsActivity).apply {
            text = title
            textSize = 15f
            setTextColor(primaryText)
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        addView(value)
        value.maxWidth = dp(if (resources.configuration.screenWidthDp >= 600) 240 else 152)
        value.maxLines = 2
        value.ellipsize = android.text.TextUtils.TruncateAt.END
    }

    private fun valueText(value: String) = TextView(this).apply {
        text = value
        textSize = 14f
        setTextColor(secondaryText)
    }

    private fun infoRow(title: String, value: String) = LinearLayout(this).apply {
        orientation = LinearLayout.HORIZONTAL
        gravity = Gravity.CENTER_VERTICAL
        setPadding(dp(16), dp(14), dp(16), dp(14))
        addView(optionBadge(title), LinearLayout.LayoutParams(dp(36), dp(36)).apply { marginEnd = dp(12) })
        addView(TextView(this@SettingsActivity).apply {
            text = title
            textSize = 15f
            setTextColor(primaryText)
        }, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        addView(valueText(value))
    }

    private fun divider() = View(this).apply {
        setBackgroundColor(if (dark) Color.rgb(48, 48, 52) else Color.rgb(230, 230, 234))
        layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(1)).apply {
            marginStart = dp(16)
        }
    }

    private fun styleSystemBars() {
        window.statusBarColor = backgroundColor
        window.navigationBarColor = backgroundColor
        if (Build.VERSION.SDK_INT >= 30) {
            val mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS or
                WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS
            window.decorView.post {
                window.insetsController?.setSystemBarsAppearance(if (dark) 0 else mask, mask)
            }
        }
    }

    private fun rounded(color: Int, radiusDp: Float) = GradientDrawable().apply {
        shape = GradientDrawable.RECTANGLE
        setColor(color)
        cornerRadius = dp(radiusDp.toInt()).toFloat()
    }

    private fun circle(color: Int) = GradientDrawable().apply {
        shape = GradientDrawable.OVAL
        setColor(color)
    }

    private fun space(heightDp: Int) = View(this).apply {
        layoutParams = LinearLayout.LayoutParams(1, dp(heightDp))
    }

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + .5f).toInt()
}
