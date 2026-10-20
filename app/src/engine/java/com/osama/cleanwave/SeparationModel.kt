package com.osama.cleanwave

import org.tensorflow.lite.Interpreter
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer

/**
 * Research-only wrapper around the carved streaming audio separation TFLite graph.
 *
 * This class intentionally lives in the engine source set. It exposes only the
 * neural graph contract; Audio's native spectral pre/post processing is reconstructed
 * separately before this can become an AudioCleaner.
 */
internal class SeparationModel(modelData: ByteBuffer, cpuThreads: Int = 1) : AutoCloseable {
    companion object {
        const val SPECTRAL_FLOATS = 1024
        const val STATE_FLOATS = 6 * 52 * 48
        const val SPECTRAL_BYTES = SPECTRAL_FLOATS * 4
        const val STATE_BYTES = STATE_FLOATS * 4
        const val OUTPUT_COUNT = 7
    }

    private val modelBuffer: ByteBuffer = modelData.slice().order(ByteOrder.nativeOrder()).also {
        require(it.isDirect) { "Audio model must use a direct or mapped buffer" }
    }

    private val options = Interpreter.Options().apply {
        // One thread remains the live default. Two-thread parity was exact on the
        // test phone, but inference became slower than the20ms budget. The optional
        // argument is retained only for the standalone numerical benchmark.
        require(cpuThreads in 1..2)
        setNumThreads(cpuThreads)
        setUseNNAPI(false)
        setUseXNNPACK(false)
    }

    private val interpreter = Interpreter(modelBuffer, options)
    private val inputLeft = directFloats(SPECTRAL_FLOATS)
    private val inputRight = directFloats(SPECTRAL_FLOATS)
    private var currentState = directFloats(STATE_FLOATS)
    private var nextState = directFloats(STATE_FLOATS)
    private val spectralOutputs = Array(4) { directFloats(SPECTRAL_FLOATS) }
    private val inputObjects = arrayOf<Any>(inputLeft, inputRight, currentState)
    private val outputMap = hashMapOf<Int, Any>(
        0 to spectralOutputs[0],
        1 to spectralOutputs[1],
        2 to nextState,
        3 to spectralOutputs[2],
        4 to spectralOutputs[3],
    )

    init {
        require(interpreter.inputTensorCount == 3) { "Unexpected Audio model input count" }
        require(interpreter.outputTensorCount == OUTPUT_COUNT) { "Unexpected Audio model output count" }
        require(interpreter.getInputTensor(0).shape().contentEquals(intArrayOf(1, 1, 1024)))
        require(interpreter.getInputTensor(1).shape().contentEquals(intArrayOf(1, 1, 1024)))
        require(interpreter.getInputTensor(2).shape().contentEquals(intArrayOf(1, 6, 52, 48)))
        require(interpreter.getOutputTensor(2).shape().contentEquals(intArrayOf(1, 6, 52, 48)))
        reset()
    }

    fun reset() {
        zero(currentState)
        zero(nextState)
    }

    /** Runs one neural invocation and copies only the two stereo heads used by Audio v2. */
    fun processInto(
        left: FloatArray,
        right: FloatArray,
        voiceLeft: FloatArray,
        voiceRight: FloatArray,
        musicLeft: FloatArray,
        musicRight: FloatArray,
    ) {
        require(left.size == SPECTRAL_FLOATS && right.size == SPECTRAL_FLOATS)
        require(
            voiceLeft.size == SPECTRAL_FLOATS && voiceRight.size == SPECTRAL_FLOATS &&
                musicLeft.size == SPECTRAL_FLOATS && musicRight.size == SPECTRAL_FLOATS
        )
        put(inputLeft, left)
        put(inputRight, right)
        currentState.rewind()
        inputObjects[2] = currentState
        spectralOutputs.forEach { it.clear() }
        nextState.clear()
        outputMap[2] = nextState
        interpreter.runForMultipleInputsOutputs(inputObjects, outputMap)

        readInto(spectralOutputs[0], voiceLeft)
        readInto(spectralOutputs[1], voiceRight)
        readInto(spectralOutputs[2], musicLeft)
        readInto(spectralOutputs[3], musicRight)
        val old = currentState
        currentState = nextState
        nextState = old
    }

    fun stateSnapshot(): FloatArray = FloatArray(STATE_FLOATS).also { readInto(currentState, it) }

    override fun close() {
        interpreter.close()
    }

    private fun directFloats(size: Int): FloatBuffer =
        ByteBuffer.allocateDirect(size * 4).order(ByteOrder.nativeOrder()).asFloatBuffer()

    private fun zero(buffer: FloatBuffer) {
        buffer.clear()
        repeat(buffer.capacity()) { buffer.put(0f) }
        buffer.rewind()
    }

    private fun put(buffer: FloatBuffer, values: FloatArray) {
        buffer.clear()
        buffer.put(values)
        buffer.rewind()
    }

    private fun readInto(buffer: FloatBuffer, result: FloatArray) {
        require(result.size == buffer.capacity())
        buffer.rewind()
        buffer.get(result)
        buffer.rewind()
    }
}
