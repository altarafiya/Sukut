package com.osama.cleanwave

import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import kotlin.math.PI
import kotlin.math.sin

/** A rounded outer flame and a golden center, both drawn behind the system س. */
internal class StrokeFlames {
    val orange = 0xFFFF791B.toInt()
    private val gold = 0xFFFFCD39.toInt()
    private val shape = Path()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    fun draw(canvas: Canvas, age: Float, growth: Float) {
        if (growth <= 0f) return
        // Reduced motion passes age zero. Keep that pose neutral, including the tips.
        val motion = (age / .45f).coerceIn(0f, 1f)
        val swell = motion * sin(age * 8.2f)
        canvas.save()
        canvas.scale(growth * (1.22f - .22f * growth), growth, 101f, 92f)
        canvas.scale(1f - .024f * swell, 1f + .032f * swell, 101f, 125f)

        drawOuter(canvas, age, motion)
        drawCore(canvas, age, motion)
        drawEmber(canvas, age, .70f, true)
        drawEmber(canvas, age, 1.12f, false)
        canvas.restore()
    }

    private fun drawOuter(canvas: Canvas, age: Float, motion: Float) {
        // The bend begins midway up the flame and travels to the tip a beat later.
        val bend = motion * 7f * sin(age * 8.4f + 1.15f)
        val lean = motion * (14f * sin(age * 8.4f) + 5f * sin(age * 15.3f - .6f))
        val lift = motion * (8f * sin(age * 10.2f - .7f) + 3f * sin(age * 18.3f))
        val leftX = motion * leftBend(age)
        val leftY = motion * leftLift(age)
        val rightX = motion * rightBend(age)
        val rightY = motion * rightLift(age)

        shape.rewind()
        shape.moveTo(101f, 135f)
        shape.cubicTo(60f, 135f, 26f, 113f, 26f, 78f)
        shape.cubicTo(26f, 55f, 40f - .35f * leftX, 43f - .32f * leftY, 39f + leftX, 22f - leftY)
        shape.quadTo(38.5f + leftX, 17f - leftY, 43f + leftX, 20f - leftY)
        shape.cubicTo(51f + .7f * leftX, 26f - .75f * leftY, 55f, 36f, 58f, 44f - .20f * leftY)
        shape.quadTo(59f, 48f - .20f * leftY, 61f, 43f - .20f * leftY)
        shape.cubicTo(65f, 28f, 69f + bend, 17f - .25f * lift, 80f + bend, 4f - .36f * lift)
        shape.cubicTo(92f + bend, -9f - .5f * lift, 99f + lean, -13f - lift, 100f + lean, -25f - lift)
        shape.quadTo(100.5f + lean, -30f - lift, 105f + lean, -27f - lift)
        shape.cubicTo(123f + lean, -16f - lift, 136f + .4f * bend, 6f, 138f, 24f - .18f * rightY)
        shape.quadTo(138.5f, 29f - .18f * rightY, 142f, 25f - .18f * rightY)
        shape.cubicTo(147f, 19f, 149f + rightX, 13f - rightY, 150f + rightX, 9f - rightY)
        shape.quadTo(151f + rightX, 5f - rightY, 154f + rightX, 9f - rightY)
        shape.cubicTo(163f + .65f * rightX, 22f - .66f * rightY, 164f, 37f, 172f, 55f)
        shape.cubicTo(192f, 101f, 161f, 135f, 101f, 135f)
        shape.close()
        paint.color = orange
        canvas.drawPath(shape, paint)
    }

    private fun drawCore(canvas: Canvas, age: Float, motion: Float) {
        // The gold crest rolls upward behind the orange tip. Its broad base holds the letter.
        val curl = motion * (8f * sin(age * 8.4f - .9f) + 2.5f * sin(age * 13.6f))
        val rise = motion * (7.5f * sin(age * 10.2f - 1.5f) + 3f * sin(age * 16.5f))
        val ripple = motion * 3.5f * sin(age * 9.7f + .6f)
        shape.rewind()
        shape.moveTo(101f, 123f)
        shape.cubicTo(67f, 123f, 44f, 106f, 45f, 82f)
        shape.cubicTo(45f, 69f, 52f, 58f, 59f, 49f - ripple)
        shape.quadTo(61.5f, 46f - ripple, 63f, 50f - ripple)
        shape.cubicTo(65f, 55f, 69f, 60f, 73f, 61f)
        shape.quadTo(76f, 63f, 79f, 59f)
        shape.cubicTo(88f, 46f, 102f + curl, 41f - rise, 104f + curl, 26f - rise)
        shape.quadTo(104.5f + curl, 22f - rise, 108f + curl, 25f - rise)
        shape.cubicTo(123f + .6f * curl, 36f - .6f * rise, 130f, 50f, 132f, 60f)
        shape.quadTo(133f, 65f, 137f, 62f)
        shape.quadTo(145f, 58f, 149f, 51f + ripple)
        shape.quadTo(151f, 47f + ripple, 153f, 52f + ripple)
        shape.cubicTo(174f, 93f, 154f, 123f, 101f, 123f)
        shape.close()
        paint.color = gold
        canvas.drawPath(shape, paint)
    }

    private fun leftBend(t: Float) = 7.4f * sin(t * 10.2f + .6f)
    private fun leftLift(t: Float) = 10f * sin(t * 11.6f) + 2.6f * sin(t * 17f)
    private fun rightBend(t: Float) = 6.5f * sin(t * 9.2f - .7f)
    private fun rightLift(t: Float) = 9f * sin(t * 10.5f + 1.5f) + 2.4f * sin(t * 16.4f)

    private fun drawEmber(canvas: Canvas, age: Float, first: Float, left: Boolean) {
        if (age < first) return
        val elapsed = (age - first) % 1.18f
        val flight = elapsed / .46f
        if (flight >= 1f) return
        // Use the tip position at launch so the ember can drift free as the tip curls back.
        val launch = age - elapsed
        val x = if (left) 40f + leftBend(launch) else 152f + rightBend(launch)
        val y = if (left) 19f - leftLift(launch) else 7f - rightLift(launch)
        val direction = if (left) -1f else 1f
        val size = sin(PI.toFloat() * flight)
        canvas.save()
        canvas.translate(x + direction * (10f * flight + 3f * size), y - 32f * flight)
        canvas.rotate(direction * (12f + 20f * flight))
        canvas.scale(size, size)
        shape.rewind()
        shape.moveTo(0f, 4f)
        shape.cubicTo(-4.5f, 4f, -4.5f, -.5f, -2.2f, -3.5f)
        shape.quadTo(.2f, -6.5f, .5f, -8.5f)
        shape.cubicTo(4f, -5f, 5f, 3f, 0f, 4f)
        shape.close()
        paint.color = orange
        canvas.drawPath(shape, paint)
        canvas.restore()
    }
}
