package com.osama.cleanwave

/** Route-session policy only; never changes AI processing or PCM. */
internal object BluetoothPerformancePolicy {
    data class Options(val effects: Boolean, val buffering: Int, val custom: Boolean)
    data class State(val device: String? = null, val previous: Options? = null)
    data class Change(val state: State, val options: Options)

    fun observe(state: State, device: String, bluetooth: Boolean, current: Options): Change {
        if (bluetooth) {
            if (state.device == device) return Change(state, current) // Respect manual override.
            return Change(State(device, state.previous ?: current), Options(true, 1, true))
        }
        return Change(State(), state.previous ?: current)
    }
}
