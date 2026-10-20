package com.osama.cleanwave

import android.animation.ValueAnimator
import android.app.Activity
import android.os.Build
import android.view.View
import android.view.animation.DecelerateInterpolator
import android.widget.FrameLayout

internal object StartupMotion {
    fun dismissLaunchWindow(activity: Activity) {
        if (Build.VERSION.SDK_INT >= 31) activity.splashScreen.setOnExitAnimationListener { splash ->
            splash.remove()
        }
    }
    fun enter(view: View, delay: Long = 0) {
        if (!ValueAnimator.areAnimatorsEnabled()) return
        view.alpha = 0f; view.translationY = 12 * view.resources.displayMetrics.density
        view.animate().alpha(1f).translationY(0f).setStartDelay(delay).setDuration(260)
            .setInterpolator(DecelerateInterpolator()).start()
    }
    fun hero(view: View) {
        if (!ValueAnimator.areAnimatorsEnabled()) return
        view.alpha = 0f; view.scaleX = .96f; view.scaleY = .96f
        view.animate().alpha(1f).scaleX(1f).scaleY(1f).setDuration(240)
            .setInterpolator(DecelerateInterpolator()).start()
    }
    fun slidePage(container: FrameLayout, page: View, direction: Int) {
        val previous = if (container.childCount == 0) null else container.getChildAt(container.childCount - 1)
        // Finish interrupted transitions before starting a new one; retain at most two pages.
        for (i in container.childCount - 1 downTo 0) {
            val child = container.getChildAt(i)
            child.animate().cancel()
            if (child !== previous) container.removeView(child)
        }
        container.addView(page, FrameLayout.LayoutParams(-1, -1))
        if (previous == null || direction == 0 || !ValueAnimator.areAnimatorsEnabled()) {
            previous?.let { container.removeView(it) }
            return
        }
        val rtl = page.resources.configuration.layoutDirection == View.LAYOUT_DIRECTION_RTL
        val distance = container.width.coerceAtLeast(page.resources.displayMetrics.widthPixels) * direction * if (rtl) -1 else 1
        page.translationX = distance.toFloat()
        previous.translationX = 0f
        previous.importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS
        previous.animate().translationX(-distance.toFloat()).setDuration(280)
            .setInterpolator(DecelerateInterpolator()).withEndAction { container.removeView(previous) }.start()
        page.animate().translationX(0f).setDuration(280).setInterpolator(DecelerateInterpolator()).start()
    }
}
