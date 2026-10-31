package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class ThemeColorsTest {
    @Test fun solidThemeIsBlackOrWhiteAndIgnoresTheSavedTint() {
        for (color in listOf(0, -1, 0xFF00FF00.toInt(), 0xFFFF00FF.toInt())) {
            assertEquals(0xFF000000.toInt(), ThemeColors.surfaces(true, false, color).background)
            assertEquals(0xFFFFFFFF.toInt(), ThemeColors.surfaces(false, false, color).background)
            assertEquals(ThemeColors.surfaces(true, false, 0), ThemeColors.surfaces(true, false, color))
        }
    }
    @Test fun tintedSurfacesAreOpaqueAndReactToTheChosenColour() {
        for (dark in listOf(false, true)) {
            val blue = ThemeColors.surfaces(dark, true, 0xFF0000FF.toInt())
            val red = ThemeColors.surfaces(dark, true, 0xFFFF0000.toInt())
            assertNotEquals(blue.background, red.background)
            for (value in listOf(blue.background, blue.card, blue.inset, blue.divider)) assertEquals(255, value ushr 24)
        }
    }
    @Test fun arbitraryPickerColoursKeepReadableAccentsAndButtonText() {
        for (dark in listOf(false, true)) for (color in listOf(0xFF000000.toInt(), -1, 0xFFFFFF00.toInt(), 0xFF0000FF.toInt(), 0xFFFF00FF.toInt(), 0xFF326A56.toInt())) {
            val surfaces = ThemeColors.surfaces(dark, true, color)
            val accent = ThemeColors.readableAccent(color, surfaces.inset, dark)
            assertTrue(ThemeColors.contrast(accent, surfaces.inset) >= 4.5)
            assertTrue(ThemeColors.contrast(accent, ThemeColors.onAccent(accent)) >= 4.5)
        }
    }
    @Test fun hexRoundTripIsOpaqueAndRejectsIncompleteOrAlphaInput() {
        assertEquals(0xFF326A56.toInt(), ThemeColors.parseHex(" #326a56 "))
        assertEquals("#326A56", ThemeColors.hex(0xFF326A56.toInt()))
        for (value in listOf("#123", "#00112233", "#GGHHII", "", "xyz")) assertNull(ThemeColors.parseHex(value))
    }
}
