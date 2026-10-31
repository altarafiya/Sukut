package com.osama.cleanwave

import org.junit.After
import org.junit.Assert.*
import org.junit.Test

class PowerActivationRequestTest {
    @After fun clearRequest() { PowerActivationRequest.cancel() }
    @Test fun aConnectionByItselfDoesNotAuthorizeAudio() {
        PowerActivationRequest.cancel()
        assertFalse(PowerActivationRequest.consumeIfReady(true))
    }
    @Test fun aPowerGestureWaitsForReadinessAndIsConsumedOnce() {
        PowerActivationRequest.request()
        assertFalse(PowerActivationRequest.consumeIfReady(false))
        assertTrue(PowerActivationRequest.consumeIfReady(true))
        assertFalse(PowerActivationRequest.consumeIfReady(true))
    }
    @Test fun backingOutCancelsTheStartEvenIfAWorkerConnectsLater() {
        PowerActivationRequest.request(); PowerActivationRequest.cancel()
        assertFalse(PowerActivationRequest.consumeIfReady(true))
    }
}
