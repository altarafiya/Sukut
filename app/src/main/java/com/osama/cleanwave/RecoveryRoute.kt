package com.osama.cleanwave

/** Pure route selection: inspect state, never change debugging or start a worker. */
internal object RecoveryRoute {
    enum class Screen { READY, NOTIFICATIONS, WORKING, DEVELOPER, TRANSPORT, ACTIVATE }
    fun screen(ready:Boolean,notificationsBlocked:Boolean,busy:Boolean,developer:Boolean,transport:Boolean)=when {
        ready->Screen.READY
        notificationsBlocked->Screen.NOTIFICATIONS
        !developer->Screen.DEVELOPER
        !transport->Screen.TRANSPORT
        busy->Screen.WORKING
        else->Screen.ACTIVATE
    }
    fun guideStep(phone:Boolean,developer:Boolean,transport:Boolean)=when {
        !developer->0
        phone && transport->1 // The code-help page, not the beginning of setup.
        phone->2             // Open Wireless debugging and pairing.
        !transport->1        // USB debugging.
        else->3              // Computer → Phone → Activate.
    }
}
