package com.osama.cleanwave

/**
 * Battery standby for a route that carries nothing.
 *
 * After [holdPackets] consecutive packets of exact digital zero input, and
 * once the processed output has itself decayed to exact zero, the cleaner may
 * skip all DSP/model work and the playback track may pause. The first packet
 * containing any nonzero sample (even 1 LSB) leaves standby and is processed
 * normally in that same packet. Loudness never gates this: quiet speech is
 * never treated as silence.
 */
internal class SilenceStandby(private val holdPackets: Int) {
    private var silentPackets = 0
    // Written by the inference worker, read by health checks on Binder threads.
    @Volatile var active = false
        private set
    var entries = 0L
        private set
    var skippedPackets = 0L
        private set

    /** Call before processing. Returns true when this packet may be skipped. */
    fun before(inputSilent: Boolean): Boolean {
        if (!inputSilent) {
            silentPackets = 0
            active = false
            return false
        }
        if (silentPackets < Int.MAX_VALUE) silentPackets++
        if (active) skippedPackets++
        return active
    }

    /** Call after a processed packet. Enters standby only once output is exactly zero. */
    fun after(outputSilent: Boolean) {
        if (!active && outputSilent && silentPackets >= holdPackets) {
            active = true
            entries++
        }
    }

    fun reset() {
        silentPackets = 0
        active = false
    }

    companion object {
        fun allZero(bytes: ByteArray): Boolean {
            for (value in bytes) if (value.toInt() != 0) return false
            return true
        }
    }
}
