package com.osama.cleanwave

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.ColorFilter
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.drawable.Drawable
import android.os.Build

/**
 * The launcher icon the user chose: one of five marks on a rounded square of one of
 * six colours. Mirrors tools/export-branding.py (same ids, same order). Android can't
 * draw a launcher icon at runtime, so each pair is a manifest activity-alias with its
 * own adaptive icon; choosing one enables that alias and disables the rest.
 */
internal object AppIcon {
    val MARKS = listOf("soft", "wave", "wordmark", "gold", "blue")
    val COLORS = listOf("black" to 0xFF000000.toInt(), "graphite" to 0xFF2B2E33.toInt(), "navy" to 0xFF0E2238.toInt(),
        "forest" to 0xFF0F3B2E.toInt(), "burgundy" to 0xFF4A1420.toInt(), "plum" to 0xFF2E1A47.toInt())
    const val DEFAULT_MARK = "blue"
    const val DEFAULT_COLOR = "black"
    private const val PREFS = "sukut_app_icon"

    fun mark(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("mark", DEFAULT_MARK)!!.takeIf { it in MARKS } ?: DEFAULT_MARK
    fun color(context: Context) = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("color", DEFAULT_COLOR)!!.takeIf { c -> COLORS.any { it.first == c } } ?: DEFAULT_COLOR
    fun colorValue(key: String) = COLORS.firstOrNull { it.first == key }?.second ?: COLORS[0].second

    fun markResource(key: String) = when (key) {
        "soft" -> R.drawable.icon_mark_soft
        "wave" -> R.drawable.icon_mark_wave
        "wordmark" -> R.drawable.icon_mark_wordmark
        "gold" -> R.drawable.icon_mark_gold
        else -> R.drawable.icon_mark_blue
    }
    fun markName(key: String) = when (key) {
        "soft" -> R.string.icon_mark_soft
        "wave" -> R.string.icon_mark_wave
        "wordmark" -> R.string.icon_mark_wordmark
        "gold" -> R.string.icon_mark_gold
        else -> R.string.icon_mark_blue
    }
    fun colorName(key: String) = when (key) {
        "graphite" -> R.string.icon_color_graphite
        "navy" -> R.string.icon_color_navy
        "forest" -> R.string.icon_color_forest
        "burgundy" -> R.string.icon_color_burgundy
        "plum" -> R.string.icon_color_plum
        else -> R.string.icon_color_black
    }

    private fun alias(context: Context, mark: String, color: String) =
        ComponentName(context.packageName, "com.osama.cleanwave.Launcher_${mark}_$color")

    /** Saves the choice and switches the launcher alias. Called only from Settings. */
    fun apply(context: Context, mark: String, color: String) {
        if (mark == mark(context) && color == color(context)) return
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putString("mark", mark).putString("color", color).apply()
        val pm = context.packageManager
        val chosen = alias(context, mark, color)
        val all = MARKS.flatMap { m -> COLORS.map { alias(context, m, it.first) } }
        // Enable first so there is never a moment without a launcher entry.
        pm.setComponentEnabledSetting(chosen, PackageManager.COMPONENT_ENABLED_STATE_ENABLED, PackageManager.DONT_KILL_APP)
        if (Build.VERSION.SDK_INT >= 33) {
            pm.setComponentEnabledSettings(all.filter { it != chosen }.map {
                PackageManager.ComponentEnabledSetting(it, PackageManager.COMPONENT_ENABLED_STATE_DISABLED, PackageManager.DONT_KILL_APP)
            })
        } else all.filter { it != chosen }.forEach {
            pm.setComponentEnabledSetting(it, PackageManager.COMPONENT_ENABLED_STATE_DISABLED, PackageManager.DONT_KILL_APP)
        }
    }

    /** The chosen icon as an in-app drawable: the same rounded square as the launcher's. */
    fun tile(context: Context, mark: String = mark(context), color: String = color(context)): Drawable =
        TileDrawable(BitmapFactory.decodeResource(context.resources, markResource(mark)), colorValue(color))

    private class TileDrawable(private val mark: Bitmap, background: Int) : Drawable() {
        private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = background }
        private val art = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        // The launcher shows the middle 72 of the 108 dp layer: draw the same region.
        private val visible = Rect(mark.width / 6, mark.height / 6, mark.width * 5 / 6, mark.height * 5 / 6)
        override fun draw(canvas: Canvas) {
            val b = RectF(bounds)
            val radius = b.width() * .225f
            canvas.drawRoundRect(b, radius, radius, fill)
            canvas.drawBitmap(mark, visible, b, art)
        }
        override fun setAlpha(alpha: Int) { fill.alpha = alpha; art.alpha = alpha; invalidateSelf() }
        override fun setColorFilter(filter: ColorFilter?) { art.colorFilter = filter; invalidateSelf() }
        @Deprecated("Deprecated in Java") override fun getOpacity() = PixelFormat.TRANSLUCENT
    }
}
