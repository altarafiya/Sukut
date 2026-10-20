package com.osama.cleanwave

import android.content.Context
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel

/** Two music-removal engines, selected for spoken dialogue or singing. */
class MusicRemovalProcessor private constructor(private val cleaner: AudioCleaner) : AudioCleaner by cleaner {
    constructor(context: Context, mode: Int) : this(
        if (BuildConfig.ENGINE_RUNTIME) createAudio(context)
        else create(mode, true) { context.assets.open(it).use { f -> f.readBytes() } }
    )

    companion object {
        const val DIALOGUE = 0
        const val SINGING = 1
        fun forFiles(directory: File, mode: Int): MusicRemovalProcessor =
            MusicRemovalProcessor(create(mode, false) { File(directory, it).readBytes() })

        private fun createAudio(context: Context): AudioCleaner {
            try {
                val type = Class.forName("com.osama.cleanwave.StreamingCleaner")
                val constructor = type.getConstructor(
                    ByteBuffer::class.java,
                    ByteArray::class.java,
                    ByteArray::class.java,
                    Boolean::class.javaPrimitiveType,
                )
                return constructor.newInstance(
                    mapAsset(context, "separator.tflite"),
                    context.assets.open("window_576_q15.bin").use { it.readBytes() },
                    context.assets.open("window_960_q15.bin").use { it.readBytes() },
                    true,
                ) as AudioCleaner
            } catch (problem: ReflectiveOperationException) {
                throw IllegalStateException("Audio research engine unavailable", problem)
            }
        }

        private fun mapAsset(context: Context, name: String): ByteBuffer =
            context.assets.openFd(name).use { descriptor ->
                check(descriptor.length > 0) { "$name is not directly mappable" }
                FileInputStream(descriptor.fileDescriptor).channel.use { channel ->
                    channel.map(FileChannel.MapMode.READ_ONLY, descriptor.startOffset, descriptor.length)
                        .order(ByteOrder.nativeOrder())
                }
            }

        private fun create(mode: Int, enforceBudget: Boolean, read: (String) -> ByteArray): AudioCleaner {
            return when (mode) {
                SINGING -> SingingCleaner(OnnxVocalModel(read("singing.onnx")), enforceBudget)
                else -> DialogueCleaner(OnnxDialogueModel(read("dialogue.onnx"), read("dialogue-states.bin")), enforceBudget)
            }
        }
    }
}

private class DialogueCleaner(private val model: OnnxDialogueModel, enforceBudget: Boolean) : AudioCleaner {
    override val sampleRate = 48000
    override val inputFrames = 512
    override val firstInputFrames = 512
    override val outputFrames = 512
    private val input = FloatArray(1024)
    private val dry = FloatArray(1024)
    private val enhanced = FloatArray(1024)
    private val delay = FloatArray(1536 * 2)
    private var delayPosition = 0
    private val level = AudioLevel()
    private val stats = CleanerMetrics("DeepFilterNet3-stereo", 512 / 48.0, enforceBudget)

    init {
        try { repeat(4) { model.process(input, enhanced) }; model.reset() }
        catch (t: Throwable) { model.close(); throw t }
    }

    override fun process(input: ByteArray, first: Boolean, output: ByteArray, backlog: Int) {
        require(input.size == inputFrames * 4 && output.size == outputFrames * 4)
        val start = System.nanoTime()
        decodeStereo(input, this.input)
        delay.copyInto(dry, 0, delayPosition, delayPosition + dry.size)
        this.input.copyInto(delay, delayPosition)
        delayPosition = (delayPosition + dry.size) % delay.size
        val work = stats.nextWork(backlog)
        // Keep recurrent state current even at 0%, so raising strength stays smooth.
        if (work != CleanerWork.BYPASS) model.process(this.input, enhanced) else dry.copyInto(enhanced)
        // Decide fallback/recovery before mixing so this packet can perform the
        // wet/dry transition while both aligned branches are still available.
        val resetState = stats.add(System.nanoTime() - start, backlog, work)
        if (first) level.resetAmount()
        level.render(dry, enhanced, output, stats.paused)
        // Missing recurrent frames makes old state invalid. Reset once when fallback begins;
        // healthy recovery probes then rebuild state before processed audio fades back in.
        if (resetState) model.reset()
    }

    override fun setReduction(percent: Int) { level.reduction = percent.coerceIn(0, 100) / 100f }
    override fun setVoiceBoost(db: Int) { level.boostDb = db.coerceIn(0, 6) }
    override fun isBypassed() = stats.paused
    override fun metrics() = stats.report(level.reduction) + "; format=48000-stereo; model_delay_samples=1536"
    override fun close() = model.close()
}

private class SingingCleaner(private val model: OnnxVocalModel, enforceBudget: Boolean) : AudioCleaner {
    override val sampleRate = 44100
    override val inputFrames = VocalSpectrogram.STEP
    override val firstInputFrames = VocalSpectrogram.WINDOW - VocalSpectrogram.TRIM
    override val outputFrames = VocalSpectrogram.STEP
    private val window = FloatArray(VocalSpectrogram.WINDOW * 2)
    private val spectral = VocalSpectrogram()
    private val features = FloatArray(VocalSpectrogram.MODEL_ELEMENTS)
    private val predicted = FloatArray(features.size)
    private val voiceWindow = FloatArray(window.size)
    private val dry = FloatArray(outputFrames * 2)
    private val voice = FloatArray(dry.size)
    private val previousTail = FloatArray(VocalSpectrogram.OVERLAP * 2)
    private val level = AudioLevel()
    private val stats = CleanerMetrics("KUIELab-MDX-vocals", inputFrames / 44.1, enforceBudget)

    init {
        try { repeat(2) { model.process(features, predicted) } }
        catch (t: Throwable) { model.close(); throw t }
    }

    override fun process(input: ByteArray, first: Boolean, output: ByteArray, backlog: Int) {
        require(input.size == (if (first) firstInputFrames else inputFrames) * 4)
        require(output.size == outputFrames * 4)
        val start = System.nanoTime()
        if (first) {
            window.fill(0f)
            decodeStereo(input, window, VocalSpectrogram.TRIM * 2)
            level.resetAmount()
        } else {
            window.copyInto(window, 0, inputFrames * 2)
            decodeStereo(input, window, (VocalSpectrogram.WINDOW - inputFrames) * 2)
        }
        window.copyInto(dry, 0, VocalSpectrogram.TRIM * 2, (VocalSpectrogram.TRIM + outputFrames) * 2)
        // At 0% Singing intentionally does no AI work; do not let cheap dry packets
        // create or satisfy an overload-recovery decision.
        val work = if (level.reduction > 0f) stats.nextWork(backlog) else CleanerWork.BYPASS
        val ranAi = work != CleanerWork.BYPASS
        if (ranAi) {
            spectral.analyze(window, features)
            model.process(features, predicted)
            spectral.synthesize(predicted, voiceWindow)
            for (i in voiceWindow.indices) voiceWindow[i] *= 1.035f // Released model's level calibration.
        } else window.copyInto(voiceWindow)
        voiceWindow.copyInto(voice, 0, VocalSpectrogram.TRIM * 2, (VocalSpectrogram.TRIM + outputFrames) * 2)
        if (!first) for (frame in 0 until VocalSpectrogram.OVERLAP) {
            val blend = frame / VocalSpectrogram.OVERLAP.toFloat()
            for (channel in 0..1) {
                val i = frame * 2 + channel
                voice[i] = previousTail[i] * (1f - blend) + voice[i] * blend
            }
        }
        voiceWindow.copyInto(previousTail, 0, (VocalSpectrogram.TRIM + outputFrames) * 2,
            (VocalSpectrogram.TRIM + VocalSpectrogram.CORE) * 2)
        stats.add(System.nanoTime() - start, backlog, if (ranAi) work else CleanerWork.BYPASS)
        level.render(dry, voice, output, stats.paused)
    }

    override fun setReduction(percent: Int) { level.reduction = percent.coerceIn(0, 100) / 100f }
    override fun setVoiceBoost(db: Int) { level.boostDb = db.coerceIn(0, 6) }
    override fun isBypassed() = stats.paused
    override fun metrics() = stats.report(level.reduction) + "; format=44100-stereo; window_frames=31744; overlap_frames=2048"
    override fun close() = model.close()
}
