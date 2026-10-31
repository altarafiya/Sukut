package com.osama.cleanwave

import android.content.Context
import android.content.res.Configuration
import java.util.Locale

object UiPrefs {
    const val THEME_SYSTEM = 0
    const val THEME_LIGHT = 1
    const val THEME_DARK = 2

    const val LANGUAGE_SYSTEM = "system"
    const val LANGUAGE_ENGLISH = "en"
    const val LANGUAGE_ARABIC = "ar"

    private const val FILE = "cleanwave_ui"
    private const val KEY_THEME = "theme"
    private const val KEY_LANGUAGE = "language"

    private fun prefs(context: Context) = context.getSharedPreferences(FILE, Context.MODE_PRIVATE)

    fun theme(context: Context): Int = prefs(context).getInt(KEY_THEME, THEME_SYSTEM)
        .takeIf { it in THEME_SYSTEM..THEME_DARK } ?: THEME_SYSTEM

    fun prepareAppearance(context: Context) {
        val state = prefs(context)
        if (state.getInt(KEY_THEME, THEME_SYSTEM) == 3)
            state.edit().putInt(KEY_THEME, THEME_SYSTEM).apply()
    }

    fun setTheme(context: Context, value: Int) {
        prefs(context).edit().putInt(KEY_THEME, value.coerceIn(THEME_SYSTEM, THEME_DARK)).apply()
    }

    fun language(context: Context): String = prefs(context).getString(KEY_LANGUAGE, LANGUAGE_SYSTEM)
        ?.takeIf { it == LANGUAGE_SYSTEM || it == LANGUAGE_ENGLISH || it == LANGUAGE_ARABIC }
        ?: LANGUAGE_SYSTEM

    fun setLanguage(context: Context, value: String) {
        val safe = if (value == LANGUAGE_ENGLISH || value == LANGUAGE_ARABIC) value else LANGUAGE_SYSTEM
        prefs(context).edit().putString(KEY_LANGUAGE, safe).apply()
    }

    fun isDark(context: Context): Boolean = when (theme(context)) {
        THEME_LIGHT -> false
        THEME_DARK -> true
        else -> (context.resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
            Configuration.UI_MODE_NIGHT_YES
    }

    fun wrapLocale(base: Context): Context {
        val code = language(base)
        if (code == LANGUAGE_SYSTEM) return base
        val locale = Locale.forLanguageTag(code)
        val config = Configuration(base.resources.configuration)
        config.setLocale(locale)
        config.setLayoutDirection(locale)
        return base.createConfigurationContext(config)
    }

    fun tinted(context: Context): Boolean = prefs(context).getBoolean("surface_tinted", false)
    fun tintColor(context: Context): Int = prefs(context).getInt("surface_tint_color", 0xFF2379B5.toInt()) or 0xFF000000.toInt()
    fun setTinted(context: Context, enabled: Boolean) { prefs(context).edit().putBoolean("surface_tinted", enabled).apply() }
    fun setTintColor(context: Context, color: Int) { prefs(context).edit().putInt("surface_tint_color", color or 0xFF000000.toInt()).apply() }
    fun signature(context: Context): String = "${theme(context)}:${language(context)}:${tinted(context)}:${tintColor(context)}"
    fun setupCompleted(context: Context): Boolean = prefs(context).getBoolean("sukut_setup_v1", false)
    fun activationMethod(context: Context): Int = prefs(context).getInt("activation_method",0).coerceIn(0,1)
    fun setActivationMethod(context: Context, method: Int) { prefs(context).edit().putInt("activation_method",method.coerceIn(0,1)).apply() }
    fun finishSetup(context: Context) { prefs(context).edit().putBoolean("sukut_setup_v1", true).apply() }
}
