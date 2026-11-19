package com.osama.cleanwave

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.ComposeShader
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.drawable.Drawable
import android.graphics.PorterDuff
import android.graphics.Shader
import android.os.SystemClock
import android.view.View
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.sin

internal class ActivationSparkView(context: Context, grey: Int) : View(context) {
    private val artwork = ActivationSparkArtwork(grey, requireNotNull(context.getDrawable(R.drawable.ic_sukut_tile)))
    private val start = SystemClock.uptimeMillis()
    private var readyAt: Long? = null

    init {
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }

    fun ready() {
        if (readyAt != null) return
        readyAt = SystemClock.uptimeMillis()
        invalidate()
    }

    override fun onWindowVisibilityChanged(visibility: Int) {
        super.onWindowVisibilityChanged(visibility)
        if (visibility == VISIBLE) invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        val now = SystemClock.uptimeMillis()
        val motion = ValueAnimator.areAnimatorsEnabled()
        artwork.draw(canvas, width.toFloat(), height.toFloat(), (now - start) / 1000f,
            readyAt?.let { (now - it) / 1000f }, motion)
        if (motion && isShown && windowVisibility == VISIBLE) postInvalidateOnAnimation()
    }
}

/** Keeps the notification and Quick Settings silhouette intact throughout the ignition. */
internal class ActivationSparkArtwork(private val grey: Int, mark: Drawable) {
    private val fire = 0xFFEF5521.toInt()
    private val emberInk = 0xFF87310E.toInt()
    private val spark = 0xFFFFD783.toInt()
    private val markBounds = RectF(19f, 5f, 181f, 167f)
    private val mask = Bitmap.createBitmap(384, 384, Bitmap.Config.ALPHA_8).also {
        mark.mutate().setBounds(0, 0, it.width, it.height)
        mark.draw(Canvas(it))
    }
    private val ink = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
    private val wave = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND; strokeJoin = Paint.Join.ROUND }
    private val streak = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeCap = Paint.Cap.ROUND }
    private val wavePath = android.graphics.Path()
    private val waveShaders = arrayOfNulls<Shader>(3)
    private val waveKeys = IntArray(3)
    private var waveWidth = 0f
    private val flames = StrokeFlames()
    private val front = LinearGradient(0f, 0f, 31f, 0f,
        flames.orange and 0x00FFFFFF, flames.orange, Shader.TileMode.CLAMP)
    private val frontMatrix = Matrix()
    private val hotMark = ComposeShader(front, BitmapShader(mask, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP).apply {
        setLocalMatrix(Matrix().apply {
            setRectToRect(RectF(0f, 0f, mask.width.toFloat(), mask.height.toFloat()), markBounds, Matrix.ScaleToFit.FILL)
        })
    }, PorterDuff.Mode.DST_IN)

    fun draw(canvas: Canvas, width: Float, height: Float, elapsed: Float, sinceReady: Float?, motion: Boolean) {
        val span = min(width * .68f, height * .69f)
        if (span <= 0f) return
        val live = if (motion) elapsed else 0f
        val progress = when {
            sinceReady == null -> 0f
            !motion -> 1f
            else -> smooth((sinceReady / .62f).coerceIn(0f, 1f))
        }
        val period = .85f
        val strike = floor(live / period).toInt()
        val age = live - strike * period
        val striking = motion && sinceReady == null && live > .25f
        val flash = if (striking) (1f - age / .28f).coerceIn(0f, 1f) else 0f
        drawBackdrop(canvas, width, height, live, flash, progress, min(width, height * .85f))

        canvas.save()
        // Make room above the letter as the flame rises.
        canvas.translate(width * .5f, height * (.55f + .12f * progress))
        canvas.scale(span / 143f, span / 143f)
        canvas.translate(-101f, -84f)

        val opened = when {
            sinceReady == null -> 0f
            !motion -> 1f
            else -> ((sinceReady - .20f) / .50f).coerceIn(0f, 1f)
        }
        val wrap = smooth(opened)
        if (sinceReady != null && opened > 0f) {
            val settle = opened - 1f
            val growth = 1f + settle * settle * (2.35f * settle + 1.35f)
            flames.draw(canvas, if (motion) sinceReady else 0f, growth)
        }

        canvas.save()
        // Nest the entire letter in the lower, wider part of the flame.
        canvas.translate(0f, 5f * wrap)
        val markScale = 1f - .40f * wrap
        canvas.scale(markScale, markScale, 101f, 84f)
        ink.shader = null
        ink.color = ThemeColors.mix(ThemeColors.mix(grey, fire, flash * .35f), emberInk, wrap)
        canvas.drawBitmap(mask, null, markBounds, ink)
        if (progress > 0f && opened < 1f) {
            frontMatrix.setTranslate(175f - 185f * progress, 0f)
            front.setLocalMatrix(frontMatrix)
            ink.color = -1
            ink.alpha = (255f * (1f - wrap)).toInt()
            ink.shader = hotMark
            canvas.drawRect(markBounds, ink)
        }
        canvas.restore()
        if (striking) drawStrike(canvas, strike, age)
        canvas.restore()
    }

    private fun drawStrike(canvas: Canvas, strike: Int, age: Float) {
        val life = .46f
        if (age >= life) return
        for (i in 0 until 5) {
            val angle = -2.5f + 2.1f * random(strike, i)
            val speed = 39f + 45f * random(strike, i + 10)
            val vx = cos(angle) * speed
            val vy = sin(angle) * speed
            val x = 164f + vx * age
            val y = 56f + vy * age + 28f * age * age
            streak.strokeWidth = .9f + .4f * random(i, 5)
            streak.color = withAlpha(spark, (225 * (1f - age / life)).toInt())
            canvas.drawLine(x - vx * .034f, y - (vy + 56f * age) * .034f, x, y, streak)
        }
    }

    private fun drawBackdrop(canvas: Canvas, width: Float, height: Float, t: Float, flash: Float, calm: Float, size: Float) {
        val w = width; val cy = height * .58f
        val steps = 64
        val segment = w / steps
        wave.strokeWidth = size * .012f
        if (waveWidth != w) { waveWidth = w; waveKeys.fill(0) }
        for (k in 0 until 3) {
            val busy = size * .11f * (.45f + .55f * kotlin.math.abs(wobble(t * 2.6f, k * 3f))) * (1f + flash * .5f)
            val amplitude = busy + (size * .022f - busy) * calm
            val speed = 1.4f + k * .5f - calm * (1f + k * .3f)
            wavePath.rewind()
            for (i in 0..steps) {
                val x = i * segment
                val edge = sin(PI * i / steps).toFloat()
                val y = cy + (k - 1) * size * .06f + amplitude * edge * sin(x / w * (5f + k * 1.7f) * 2 * PI.toFloat() + t * speed * 2 * PI.toFloat() + k * 1.9f)
                if (i == 0) wavePath.moveTo(x, y) else wavePath.lineTo(x, y)
            }
            // One path per wave; the fade towards both edges comes from a cached gradient,
            // rebuilt only while the colour is changing (the 0.6 s after ready).
            val key = withAlpha(ThemeColors.mix(grey, fire, calm), ((if (k == 0) 52 else 32) * (1f - .40f * calm)).toInt())
            if (waveKeys[k] != key) {
                waveKeys[k] = key
                waveShaders[k] = LinearGradient(0f, 0f, w, 0f, intArrayOf(key and 0x00FFFFFF, withAlpha(key, (key ushr 24) * 7 / 10), key, withAlpha(key, (key ushr 24) * 7 / 10), key and 0x00FFFFFF),
                    floatArrayOf(0f, .25f, .5f, .75f, 1f), Shader.TileMode.CLAMP)
            }
            wave.shader = waveShaders[k]
            canvas.drawPath(wavePath, wave)
        }
        wave.shader = null
        // Motes drifting slowly upward.
        for (i in 0 until 14) {
            val life = 5f + random(i, 31) * 4f
            val p = ((t + random(i, 32) * life) / life) % 1f
            val x = w * random(i, 33) + sin(t * .6f + i).toFloat() * size * .03f
            val y = height * (1f - p)
            val colour = ThemeColors.mix(grey, spark, calm)
            dot.color = withAlpha(colour, (60 * sin(PI * p).toFloat()).toInt())
            canvas.drawCircle(x, y, size * (.006f + random(i, 34) * .008f), dot)
        }
    }

    /** A smooth wobble in [-1, 1] from three unrelated sines. */
    private fun wobble(t: Float, k: Float) = (sin(t * 2.3 + k) * .5 + sin(t * 3.9 + k * 1.7) * .3 + sin(t * 6.7 + k * 2.9) * .2).toFloat()
    private fun random(a: Int, b: Int): Float { val v = sin(a * 12.9898 + b * 78.233) * 43758.5453; return (v - floor(v)).toFloat() }
    private fun withAlpha(color: Int, alpha: Int) = (color and 0x00FFFFFF) or (alpha.coerceIn(0, 255) shl 24)
    private fun smooth(x: Float) = x * x * (3f - 2f * x)
}
