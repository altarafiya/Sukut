package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.DataInputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder

class AdbWireTest {
    @Test fun packetRoundTripPreservesStreamAndPayload() {
        val out = ByteArrayOutputStream()
        AdbWire.write(out, AdbWire.WRTE, 11, 1, byteArrayOf(0, 127, -1))
        val packet = AdbWire.read(DataInputStream(ByteArrayInputStream(out.toByteArray())))
        assertEquals(AdbWire.WRTE, packet.command); assertEquals(11, packet.arg0); assertEquals(1, packet.arg1)
        assertArrayEquals(byteArrayOf(0, 127, -1), packet.data)
    }
    @Test fun rejectsOversizedAndCorruptPacketsBeforeAllocation() {
        fun packet(size: Int, magic: Int) = ByteBuffer.allocate(24).order(ByteOrder.LITTLE_ENDIAN)
            .putInt(AdbWire.WRTE).putInt(1).putInt(1).putInt(size).putInt(0).putInt(magic).array()
        for (bytes in listOf(packet(-1, AdbWire.WRTE.inv()), packet(AdbWire.MAX + 1, AdbWire.WRTE.inv()), packet(0, 123))) {
            assertThrows(IllegalArgumentException::class.java) { AdbWire.read(DataInputStream(ByteArrayInputStream(bytes))) }
        }
        val out = ByteArrayOutputStream(); AdbWire.write(out, AdbWire.WRTE, data = byteArrayOf(1))
        val corrupt = out.toByteArray(); corrupt[24] = 2
        assertThrows(IllegalArgumentException::class.java) { AdbWire.read(DataInputStream(ByteArrayInputStream(corrupt))) }
    }
    @Test fun pairingInputsAcceptOnlySixAsciiDigitsAndUnprivilegedPorts() {
        assertTrue(AdbWire.validCode("012345"))
        listOf("12345", "1234567", "123 56", "١٢٣٤٥٦", "12345\n").forEach { assertFalse(AdbWire.validCode(it)) }
        listOf(-1, 0, 22, 1023, 65536).forEach { assertFalse(AdbWire.validPort(it)) }
        assertTrue(AdbWire.validPort(1024)); assertTrue(AdbWire.validPort(65535))
    }
}
