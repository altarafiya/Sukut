package com.osama.cleanwave

import java.io.DataInputStream
import java.io.OutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

/** Small, bounded subset of ADB: TLS negotiation and one fixed shell stream. */
internal object AdbWire {
    const val CNXN = 0x4e584e43
    const val STLS = 0x534c5453
    const val AUTH = 0x48545541
    const val OPEN = 0x4e45504f
    const val OKAY = 0x59414b4f
    const val WRTE = 0x45545257
    const val CLSE = 0x45534c43
    const val MAX = 1024 * 1024
    data class Packet(val command: Int, val arg0: Int, val arg1: Int, val data: ByteArray)
    fun read(input: DataInputStream): Packet {
        val header = ByteArray(24); input.readFully(header)
        val h = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN)
        val command = h.int; val a = h.int; val b = h.int; val size = h.int; val checksum = h.int; val magic = h.int
        require(magic == command.inv() && size in 0..MAX) { "Invalid local ADB packet" }
        val data = ByteArray(size); input.readFully(data)
        require(checksum == 0 || checksum == data.sumOf { it.toInt() and 255 }) { "Invalid local ADB checksum" }
        return Packet(command, a, b, data)
    }
    fun write(output: OutputStream, command: Int, a: Int = 0, b: Int = 0, data: ByteArray = byteArrayOf()) {
        require(data.size <= MAX)
        output.write(ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN).putInt(command).putInt(a).putInt(b)
            .putInt(data.size).putInt(data.sumOf { it.toInt() and 255 }).putInt(command.inv()).array())
        output.write(data); output.flush()
    }
    fun validCode(code: String) = code.length == 6 && code.all { it in '0'..'9' }
    fun validPort(port: Int) = port in 1024..65535
}
