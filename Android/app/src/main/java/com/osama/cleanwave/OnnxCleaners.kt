package com.osama.cleanwave

import ai.onnxruntime.OnnxTensor
import ai.onnxruntime.OrtEnvironment
import ai.onnxruntime.OrtSession
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

internal fun directFloats(size: Int): FloatBuffer =
    ByteBuffer.allocateDirect(size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

internal fun sessionOptions(threads: Int) = OrtSession.SessionOptions().apply {
    setIntraOpNumThreads(threads)
    setInterOpNumThreads(1)
    setOptimizationLevel(OrtSession.SessionOptions.OptLevel.ALL_OPT)
}

internal class OnnxVocalModel(bytes: ByteArray) : AutoCloseable {
    private val env = OrtEnvironment.getEnvironment()
    private val session = sessionOptions(4).use { env.createSession(bytes, it) }
    private val input = directFloats(VocalSpectrogram.MODEL_ELEMENTS)
    private val tensor = OnnxTensor.createTensor(env, input, longArrayOf(1, 4, 2048, 32))
    private val feeds = mapOf("input" to tensor)

    fun process(source: FloatArray, destination: FloatArray) {
        input.rewind(); input.put(source); input.rewind()
        session.run(feeds).use { result ->
            (result[0] as OnnxTensor).floatBuffer.get(destination)
        }
    }

    override fun close() { tensor.close(); session.close() }
}

internal class OnnxDialogueModel(bytes: ByteArray, initialBytes: ByteArray) : AutoCloseable {
    private val env = OrtEnvironment.getEnvironment()
    private val session = sessionOptions(1).use { env.createSession(bytes, it) }
    private val initial = ByteBuffer.wrap(initialBytes).order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
    private val input = Array(2) { directFloats(512) }
    private val states = Array(2) {
        DialogueStateSpec.shapes.map { shape -> directFloats(shape.fold(1L) { a, b -> a * b }.toInt()) }
    }
    private val tensors = Array(2) { ch ->
        LinkedHashMap<String, OnnxTensor>().apply {
            put("input_frame", OnnxTensor.createTensor(env, input[ch], longArrayOf(512)))
            for (i in DialogueStateSpec.names.indices)
                put(DialogueStateSpec.names[i], OnnxTensor.createTensor(env, states[ch][i], DialogueStateSpec.shapes[i]))
        }
    }
    private val mono = FloatArray(512)
    private val resultMono = FloatArray(512)

    init { reset() }

    fun reset() {
        for (channel in 0..1) {
            initial.rewind()
            for (state in states[channel]) {
                state.rewind()
                repeat(state.capacity()) { state.put(initial.get()) }
                state.rewind()
            }
        }
    }

    fun process(stereo: FloatArray, enhanced: FloatArray) {
        for (channel in 0..1) {
            for (i in mono.indices) mono[i] = stereo[i * 2 + channel]
            input[channel].rewind(); input[channel].put(mono); input[channel].rewind()
            session.run(tensors[channel]).use { result ->
                (result[0] as OnnxTensor).floatBuffer.get(resultMono)
                for (i in resultMono.indices) enhanced[i * 2 + channel] = resultMono[i]
                for (i in states[channel].indices) {
                    val state = states[channel][i]
                    state.rewind(); state.put((result[i + 1] as OnnxTensor).floatBuffer); state.rewind()
                }
            }
        }
    }

    override fun close() {
        for (channel in tensors) for (tensor in channel.values) tensor.close()
        session.close()
    }
}
