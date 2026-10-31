package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class ActivationNoticeTest {
    @Test fun settingsCanWaitForPostedNotificationAndCompletionClearsReadiness() {
        LocalActivationState.update(1,true)
        assertTrue(LocalActivationState.busy); assertFalse(LocalActivationState.notificationReady)
        LocalActivationState.update(2,true,posted=true)
        assertTrue(LocalActivationState.notificationReady)
        LocalActivationState.update(3,false,posted=true)
        assertFalse(LocalActivationState.busy); assertFalse(LocalActivationState.notificationReady)
    }
    @Test fun wiredOutputsDoNotTriggerTheBluetoothNotice() {
        listOf(0,1,2,3,4,9,11,22).forEach { assertFalse(BluetoothAudioDevices.isBluetooth(it)) }
    }
    @Test fun classicAndLowEnergyBluetoothOutputsAreRecognized() {
        listOf(7,8,23,26,27,30).forEach { assertTrue(BluetoothAudioDevices.isBluetooth(it)) }
    }
}
