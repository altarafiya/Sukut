package com.osama.cleanwave

internal data class EngineHealth(val running: Boolean, val detail: String, val metrics: String) {
    companion object {
        fun parse(values: Array<String>): EngineHealth {
            require(values.size == 3 && values[0] in listOf("true", "false")) { "Invalid audio health reply" }
            return EngineHealth(values[0] == "true", values[1], values[2])
        }
    }
}
