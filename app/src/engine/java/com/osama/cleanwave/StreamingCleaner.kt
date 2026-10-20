package com.osama.cleanwave

import java.nio.ByteBuffer
import kotlin.math.pow
import kotlin.math.roundToInt

/** Private research cleaner using the carved Audio separator graph and reconstructed DSP. */
class StreamingCleaner(
    modelBuffer: ByteBuffer,
    riseWindowBytes: ByteArray,
    hopWindowBytes: ByteArray,
    enforceBudget: Boolean,
) : AudioCleaner {
    override val sampleRate = StreamingDsp.SAMPLE_RATE
    override val inputFrames = StreamingDsp.HOP_FRAMES
    override val firstInputFrames = StreamingDsp.FIRST_INPUT_FRAMES
    override val outputFrames = StreamingDsp.HOP_FRAMES

    private val dsp = StreamingDsp(modelBuffer, riseWindowBytes, hopWindowBytes)
    private val firstDecoded = FloatArray(firstInputFrames * 2)
    private val hopDecoded = FloatArray(inputFrames * 2)
    private val dry = FloatArray(outputFrames * 2)
    private val wet = FloatArray(outputFrames * 2)
    private val level = OutputMixer()
    private val stats = CleanerMetrics(
        "StereoSeparator",
        outputFrames / 48.0,
        enforceBudget,
        fallbackOnBacklog = false,
        recoveryCooldownMs = 40.0,
        maxRecoveryCooldownMs = 260.0,
        detailedTimings = false,
    )
    private data class Mix(val voice: Int, val music: Int, val other: Int)
    @Volatile private var mix = Mix(4096, 1295, 410)

    init {
        // Warm both JTransforms and the TFLite graph before the live route starts.
        // Cold first invocations can briefly exceed the 20 ms hop budget and create
        // avoidable queue backlog even though steady-state inference is comfortably faster.
        dsp.process(firstDecoded, true, true, mix.voice, mix.music, mix.other, dry, wet)
        repeat(3) {
            dsp.process(hopDecoded, false, true, mix.voice, mix.music, mix.other, dry, wet)
        }
        dsp.resetAll()
    }

    override fun process(input: ByteArray, first: Boolean, output: ByteArray, backlog: Int) {
        val frames = if (first) firstInputFrames else inputFrames
        require(input.size == frames * 4 && output.size == outputFrames * 4)
        val start = System.nanoTime()
        val decoded = if (first) firstDecoded else hopDecoded
        decodeStereo(input, decoded)
        val work = stats.nextWork(backlog)
        val runAi = work != CleanerWork.BYPASS
        val current = mix
        dsp.process(
            input = decoded,
            first = first,
            runModel = runAi,
            // Authentic Voice Focus suppression without Audio's +6 dB voice gain;
            // CleanWave's existing explicit voice-boost control handles user gain separately.
            voiceQ12 = current.voice,
            musicQ12 = current.music,
            otherQ12 = current.other,
            dryOutput = dry,
            wetOutput = wet,
        )
        val resetState = stats.add(System.nanoTime() - start, backlog, work)
        level.render(dry, wet, output, stats.paused, runAi)
        if (resetState) dsp.resetNeuralState()
    }

    override fun setReduction(percent: Int) {
        level.reduction = percent.coerceIn(0, 100) / 100f
    }

    override fun setVoiceBoost(db: Int) {
        // Source voice gain is applied inside the spectral remix, once.
    }

    override fun setSourceMix(voiceDb: Int, musicDb: Int, otherDb: Int) {
        mix = Mix(q12(voiceDb), q12(musicDb), q12(otherDb))
    }

    override fun setAmplification(db: Int) { level.setAmplification(db) }
    override fun setSilentRecovery(silent: Boolean) { level.silentRecovery = silent }
    // Consume the selected timing reserve before interrupting the model for
    // recovery. Healthy packets repay this debt after short scheduler spikes.
    override fun setBuffering(profile: Int) { stats.overloadCreditMs = when (profile) { 1 -> 60.0; 2 -> 100.0; else -> 20.0 } }

    override fun isBypassed() = stats.paused

    override fun metrics() = stats.report(level.reduction) +
        "; format=48000-stereo; audio_fft=1536; hop_frames=960; overlap_frames=576; declared_effect_latency_ms=32"

    override fun close() = dsp.close()

    private fun q12(db: Int): Int =
        if (db <= -100) 0 else (4096.0 * 10.0.pow(db.coerceIn(-100, 10) / 20.0)).roundToInt()
}
