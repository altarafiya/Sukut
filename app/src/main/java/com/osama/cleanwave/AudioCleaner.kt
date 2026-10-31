package com.osama.cleanwave

/** Fixed packets per route, stereo PCM16. First packet can include model look-ahead. */
interface AudioCleaner : AutoCloseable {
    val sampleRate: Int
    val inputFrames: Int
    val firstInputFrames: Int
    val outputFrames: Int
    fun process(input: ByteArray, first: Boolean, output: ByteArray, backlog: Int)
    fun setReduction(percent: Int)
    fun setVoiceBoost(db: Int)
    fun setSourceMix(voiceDb: Int, musicDb: Int, otherDb: Int) = Unit
    fun setAmplification(db: Int) = Unit
    fun setSilentRecovery(silent: Boolean) = Unit
    fun setBuffering(profile: Int) = Unit
    fun setScreenOff(off: Boolean) = Unit
    fun isBypassed(): Boolean
    /** True while nothing is playing and the route may rest (no model work, track paused). */
    fun isStandby(): Boolean = false
    fun metrics(): String
    override fun close()
}
