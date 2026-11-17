package com.osama.cleanwave

import android.app.Activity
import android.app.Dialog
import android.content.Context
import android.content.SharedPreferences
import android.view.View
import android.widget.LinearLayout

internal object EnginePerformance {
    /** delay and smooth are 1..3 for the plain-language meter; 0 means "your choice". */
    data class Mode(val label: Int, val detail: Int, val icon: Int, val summary: Int, val delay: Int = 0, val smooth: Int = 0)
    val modes = listOf(
        Mode(R.string.performance_lowest, R.string.performance_lowest_detail, R.drawable.ic_fast_output, R.string.performance_lowest_short, 1, 1),
        Mode(R.string.performance_balanced, R.string.performance_balanced_detail, R.drawable.ic_buffer, R.string.performance_balanced_short, 2, 2),
        Mode(R.string.performance_wireless, R.string.performance_quality_detail, R.drawable.ic_headphones, R.string.performance_quality_short, 3, 3),
        Mode(R.string.performance_custom, R.string.performance_custom_detail, R.drawable.ic_sliders, R.string.performance_custom_short),
    )
    fun meter(context: Context, mode: Mode): CharSequence? {
        if (mode.delay == 0) return null
        fun dots(level: Int) = "●".repeat(level) + "○".repeat(3 - level)
        return context.getString(R.string.meter_delay) + "  " + dots(mode.delay) + "     " +
            context.getString(R.string.meter_smooth) + "  " + dots(mode.smooth)
    }
    fun selected(context: Context) = EnginePerformanceConfig.mode(CleanerPrefs.effectsCompatibility(context), CleanerPrefs.buffering(context), CleanerPrefs.customPerformance(context))
    fun select(context: Context, mode: Int) {
        require(mode in modes.indices)
        if (mode == EnginePerformanceConfig.CUSTOM)
            CleanerPrefs.setEnginePerformance(context, CleanerPrefs.effectsCompatibility(context), CleanerPrefs.buffering(context), custom = true)
        else EnginePerformanceConfig.presets[mode].let { CleanerPrefs.setEnginePerformance(context, it.effects, it.buffering) }
    }
    fun increaseBuffering(context: Context): Boolean {
        val next = CleanerPrefs.buffering(context) + 1
        if (next > 2) return false
        val effects = CleanerPrefs.effectsCompatibility(context)
        CleanerPrefs.setEnginePerformance(context, effects, next,
            CleanerPrefs.customPerformance(context) || EnginePerformanceConfig.mode(effects, next) == EnginePerformanceConfig.CUSTOM)
        return true
    }

    fun bufferLabel(context: Context) = context.getString(listOf(R.string.buffer_lowest, R.string.buffer_balanced, R.string.buffer_maximum)[CleanerPrefs.buffering(context)])
    fun effectsSummary(context: Context) = context.getString(if (CleanerPrefs.effectsCompatibility(context)) R.string.effects_on else R.string.effects_off)

    fun choices(activity: Activity, onChanged: () -> Unit = {}): View {
        val ui = MobileUi(activity)
        val panel = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; tag = "engine_performance_choices" }
        val audio=activity.getSystemService(android.media.AudioManager::class.java)
        val bluetoothNote=ui.notice(ui.palette.warningContainer).apply {
            tag="phone_effects_bluetooth_note"
            addView(ui.text(TextAccents.color(activity,activity.getString(R.string.phone_effects_delay)),13f,color=ui.warning))
        }
        var applying = false
        val effects = SukutSwitch(activity).apply {
            tag = "phone_effects_switch"
            contentDescription = activity.getString(R.string.phone_effects)
            isChecked = CleanerPrefs.effectsCompatibility(activity)
        }
        panel.addView(ui.card(16).apply {
            addView(LinearLayout(activity).apply {
                gravity = android.view.Gravity.CENTER_VERTICAL
                addView(ui.icon(R.drawable.ic_headphones, ui.accent, 23), LinearLayout.LayoutParams(ui.dp(23), ui.dp(23)).apply { marginEnd = ui.dp(12) })
                addView(ui.text(R.string.phone_effects, 16f, true), LinearLayout.LayoutParams(0, -2, 1f).apply { marginEnd = ui.dp(12) })
                addView(effects, LinearLayout.LayoutParams(-2, -2))
            })
            addView(bluetoothNote,LinearLayout.LayoutParams(-1,-2).apply { topMargin=ui.dp(12) })
        })
        val buffering = SegmentedChoice(activity,
            listOf(R.string.buffer_lowest, R.string.buffer_balanced, R.string.buffer_maximum).map(activity::getString),
            CleanerPrefs.buffering(activity), (0..2).map { ModeStyle.buffer(it, ui.dark) }) { which ->
            if (!applying) { CleanerPrefs.setEnginePerformance(activity, effects.isChecked, which, custom = true); onChanged() }
        }.apply { tag = "audio_buffering" }
        val bufferIcon = ui.icon(ModeStyle.bufferIcon(CleanerPrefs.buffering(activity)), ModeStyle.buffer(CleanerPrefs.buffering(activity), ui.dark), 23).apply { tag = "buffering_icon" }
        panel.addView(ui.card(16).apply {
            addView(LinearLayout(activity).apply {
                gravity = android.view.Gravity.CENTER_VERTICAL
                addView(bufferIcon, LinearLayout.LayoutParams(ui.dp(23), ui.dp(23)).apply { marginEnd = ui.dp(12) })
                addView(ui.text(R.string.buffering, 16f, true), LinearLayout.LayoutParams(0, -2, 1f))
            })
            addView(ui.text(R.string.buffering_short_note, 13f, color = ui.secondary).apply { setPadding(0, ui.dp(6), 0, ui.dp(14)) })
            addView(buffering, LinearLayout.LayoutParams(-1, -2))
        }, LinearLayout.LayoutParams(-1, -2).apply { topMargin = ui.dp(12) })
        effects.setOnCheckedChangeListener { _, checked ->
            if (!applying) { CleanerPrefs.setEnginePerformance(activity, checked, buffering.selected, custom = true); onChanged() }
        }
        fun refresh() {
            applying = true
            effects.isChecked = CleanerPrefs.effectsCompatibility(activity)
            buffering.select(CleanerPrefs.buffering(activity))
            bufferIcon.setImageResource(ModeStyle.bufferIcon(buffering.selected))
            bufferIcon.imageTintList = android.content.res.ColorStateList.valueOf(ModeStyle.buffer(buffering.selected, ui.dark))
            applying = false
            bluetoothNote.visibility=if(BluetoothAudioDevices.connected(activity))View.VISIBLE else View.GONE
        }
        val deviceChanged=object:android.media.AudioDeviceCallback() {
            override fun onAudioDevicesAdded(devices:Array<out android.media.AudioDeviceInfo>) { refresh() }
            override fun onAudioDevicesRemoved(devices:Array<out android.media.AudioDeviceInfo>) { refresh() }
        }
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == "effects_compatibility" || key == "buffering") panel.post { if (panel.isAttachedToWindow) refresh() }
        }
        panel.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(view: View) {
                CleanerPrefs.registerListener(activity,listener)
                audio.registerAudioDeviceCallback(deviceChanged,android.os.Handler(android.os.Looper.getMainLooper())); refresh()
            }
            override fun onViewDetachedFromWindow(view: View) {
                CleanerPrefs.unregisterListener(activity,listener); audio.unregisterAudioDeviceCallback(deviceChanged)
            }
        })
        return panel
    }
    fun show(activity: Activity, onChanged: () -> Unit = {}): Dialog {
        val ui = MobileUi(activity)
        var dialog: Dialog? = null
        var help: Dialog? = null
        val content = LinearLayout(activity).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(ui.dp(20), ui.dp(16), ui.dp(20), ui.dp(20))
            addView(LinearLayout(activity).apply {
                gravity = android.view.Gravity.CENTER_VERTICAL
                addView(ui.text(R.string.engine_performance, 23f, true).apply { isAccessibilityHeading = true }, LinearLayout.LayoutParams(0, -2, 1f))
                addView(ui.text("?", 22f, true, ui.accent).apply {
                    tag = "performance_help"; gravity = android.view.Gravity.CENTER
                    contentDescription = activity.getString(R.string.performance_help_title)
                    background = ui.pressable(ui.palette.cardAlt, 24); isFocusable = true
                    setOnClickListener {
                        help?.dismiss()
                        help = showHelp(activity).also { opened -> opened.setOnDismissListener { if (help === opened) help = null } }
                    }
                }, LinearLayout.LayoutParams(ui.dp(48), ui.dp(48)))
            })
            addView(ui.space(16)); addView(choices(activity, onChanged))
        }
        content.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(view: View) {}
            override fun onViewDetachedFromWindow(view: View) { help?.dismiss(); help = null }
        })
        val done = ui.button(R.string.performance_done, 0, MobileUi.Tone.FILLED) { dialog?.dismiss() }
        return ui.sheet(content, done).also { dialog = it }
    }
    private fun showHelp(activity: Activity): Dialog = AudioHelp.showPerformance(activity)
}
