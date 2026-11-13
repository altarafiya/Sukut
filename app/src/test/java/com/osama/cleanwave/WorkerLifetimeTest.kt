package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class WorkerLifetimeTest {
    @Test fun aNewUnixSessionDoesNotMakeAnInheritedServiceCgroupIndependent() {
        assertTrue(WorkerLifetime.inheritedServiceGroup("0::/system/uid_0/pid_100\n",200))
        assertTrue(WorkerLifetime.inheritedServiceGroup("0::/uid_2000/pid_100/child\n",200))
        assertFalse(WorkerLifetime.inheritedServiceGroup("0::/system/uid_2000/pid_200\n",200))
        assertFalse(WorkerLifetime.inheritedServiceGroup("0::/\n3:cpu:/system\n",200))
        assertTrue(WorkerLifetime.inheritedServiceGroup("",200))
    }
}
