package com.osama.cleanwave

/** In-process user gesture, never accepted from an exported Intent. */
internal object PowerActivationRequest {
    private var pending=false
    fun request() { pending=true }
    fun cancel() { pending=false }
    fun consumeIfReady(ready:Boolean=WorkerConnection.ready):Boolean {
        if(!pending || !ready)return false
        pending=false; return true
    }
}
