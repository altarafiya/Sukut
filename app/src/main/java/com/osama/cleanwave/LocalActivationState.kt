package com.osama.cleanwave

/** Only short status text, never pairing codes/keys. No retained Activity. */
internal object LocalActivationState {
    var message = 0; private set
    var busy = false; private set
    var notificationReady = false; private set
    private val listeners = LinkedHashSet<() -> Unit>()
    fun add(listener: () -> Unit) { listeners.add(listener) }
    fun remove(listener: () -> Unit) { listeners.remove(listener) }
    fun update(text: Int, running: Boolean, posted: Boolean = false) {
        message = text; busy = running; notificationReady = running && posted
        listeners.toList().forEach { it() }
    }
}
