package com.osama.cleanwave

import java.lang.reflect.InvocationTargetException
import org.junit.Assert.*
import org.junit.Test

class WorkerErrorsTest {
    @Test fun frameworkReflectionPreservesTheActualRoutingFailure() {
        val cause = IllegalArgumentException("Invalid route")
        assertEquals("IllegalArgumentException: Invalid route", WorkerErrors.concise(InvocationTargetException(InvocationTargetException(cause))))
    }
    @Test fun exceptionsWithoutMessagesStillHaveAUsefulType() {
        assertEquals("IllegalStateException", WorkerErrors.concise(IllegalStateException()))
    }
}
