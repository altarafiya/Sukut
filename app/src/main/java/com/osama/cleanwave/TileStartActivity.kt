package com.osama.cleanwave

import android.app.Activity
import android.os.Bundle

/**
 * Invisible and finished at once: the Quick Settings tile opens it when Android
 * refuses to start the filter from the background, because an activity in the
 * foreground may start a foreground service.
 */
class TileStartActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        PlaybackController.enable(this)
        finish()
    }
}
