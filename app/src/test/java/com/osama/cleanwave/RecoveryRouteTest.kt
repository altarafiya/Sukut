package com.osama.cleanwave
import org.junit.Assert.*
import org.junit.Test
class RecoveryRouteTest {
    @Test fun activeWorkerAlwaysWinsOverEverySetupCondition() {
        for(dev in listOf(false,true))for(transport in listOf(false,true))for(busy in listOf(false,true))
            assertEquals(RecoveryRoute.Screen.READY,RecoveryRoute.screen(true,true,busy,dev,transport))
    }
    @Test fun inactiveRecoveryShowsOnlyItsCurrentAction() {
        assertEquals(RecoveryRoute.Screen.DEVELOPER,RecoveryRoute.screen(false,false,true,false,false))
        assertEquals(RecoveryRoute.Screen.TRANSPORT,RecoveryRoute.screen(false,false,true,true,false))
        assertEquals(RecoveryRoute.Screen.WORKING,RecoveryRoute.screen(false,false,true,true,true))
        assertEquals(RecoveryRoute.Screen.ACTIVATE,RecoveryRoute.screen(false,false,false,true,true))
        assertEquals(RecoveryRoute.Screen.NOTIFICATIONS,RecoveryRoute.screen(false,true,false,true,true))
    }
    @Test fun guideStartsAtCodeHelpAfterWirelessIsOn() {
        assertEquals(1,RecoveryRoute.guideStep(true,true,true))
        assertEquals(0,RecoveryRoute.guideStep(true,false,false))
        assertEquals(2,RecoveryRoute.guideStep(true,true,false))
        assertEquals(1,RecoveryRoute.guideStep(false,true,false))
        assertEquals(3,RecoveryRoute.guideStep(false,true,true))
    }
}
