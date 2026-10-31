package com.osama.cleanwave

import android.content.Context
import android.media.AudioDeviceInfo
import android.media.AudioManager

/** Public audio-device API only; no Bluetooth names, addresses or scan permission. */
internal object BluetoothAudioDevices {
    fun isBluetooth(type:Int)=when(type) {
        AudioDeviceInfo.TYPE_BLUETOOTH_A2DP,AudioDeviceInfo.TYPE_BLUETOOTH_SCO,AudioDeviceInfo.TYPE_HEARING_AID,
        AudioDeviceInfo.TYPE_BLE_HEADSET,AudioDeviceInfo.TYPE_BLE_SPEAKER,AudioDeviceInfo.TYPE_BLE_BROADCAST -> true
        else -> false
    }
    fun connected(context:Context)=context.getSystemService(AudioManager::class.java)
        .getDevices(AudioManager.GET_DEVICES_OUTPUTS).any { isBluetooth(it.type) }
}
