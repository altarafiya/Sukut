package com.osama.cleanwave

import android.content.Intent
import android.content.res.Configuration
import android.content.pm.PackageManager
import android.view.ContextThemeWrapper
import android.view.View
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import android.widget.TextView
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.util.Locale

@RunWith(AndroidJUnit4::class)
class SukutBetaContractTest {
    private val ins = InstrumentationRegistry.getInstrumentation()
    private val context get() = ins.targetContext

    @Test fun longPressTileTargetsTheAppRatherThanSystemAppInfo() {
        val intent = Intent("android.service.quicksettings.action.QS_TILE_PREFERENCES")
            .setPackage(context.packageName).addCategory(Intent.CATEGORY_DEFAULT)
        val resolved = context.packageManager.resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY)
        assertNotNull("Long-press tile has no app activity", resolved)
        assertEquals(MainActivity::class.java.name, resolved!!.activityInfo.name)
    }

    @Test fun languageChoicesUseAutonymsInBothLocales() {
        for (language in listOf("ar", "en")) {
            val configuration = Configuration(context.resources.configuration).apply { setLocale(Locale(language)) }
            val localized = context.createConfigurationContext(configuration)
            assertEquals("English", localized.getString(R.string.english))
            assertEquals("العربية", localized.getString(R.string.arabic))
        }
    }

    @Test fun arabicButtonCanWrapAtLargeFontSize() {
        ins.runOnMainSync {
            val configuration = Configuration(context.resources.configuration).apply {
                fontScale = 1.8f; setLocale(Locale("ar")); setLayoutDirection(Locale("ar"))
            }
            val theme = ContextThemeWrapper(context.createConfigurationContext(configuration), R.style.Theme_CleanWave_Light)
            // Sukut's buttons are framework TextViews styled by MobileUi.button.
            val button = TextView(theme).apply {
                text = theme.getString(R.string.reconnect_worker); textSize = 14f
                isAllCaps = false; isSingleLine = false; maxLines = 3; ellipsize = null
                SukutTypography.apply(this)
            }
            val width = (220 * theme.resources.displayMetrics.density).toInt()
            button.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.AT_MOST))
            button.layout(0, 0, button.measuredWidth, button.measuredHeight)
            assertTrue(button.layout.lineCount > 0)
            assertEquals(0, button.layout.getEllipsisCount(button.layout.lineCount - 1))
            assertTrue(button.layout.getLineBottom(button.layout.lineCount - 1) <=
                button.height - button.compoundPaddingTop - button.compoundPaddingBottom)
        }
    }
}
