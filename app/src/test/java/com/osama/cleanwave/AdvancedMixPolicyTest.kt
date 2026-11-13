package com.osama.cleanwave

import org.junit.Assert.*
import org.junit.Test

class AdvancedMixPolicyTest {
    // The real code is not in the source; the policy is tested with a stand-in.
    private val CODE = "test-code"
    private val matches: (String) -> Boolean = { it == CODE }
    @Test fun onlyTheHashedCodeIsAccepted() {
        assertFalse(AdvancedMixPolicy.isAccessCode("wrong"))
        assertFalse(AdvancedMixPolicy.isAccessCode(""))
        System.getenv("SUKUT_ADVANCED_CODE")?.let { assertTrue(AdvancedMixPolicy.isAccessCode(it)) }
    }
    @Test fun enablingRequiresFiveSecondsAndLeavingAdvancedRequiresThree() {
        assertEquals(5000L,AdvancedMixPolicy.holdMillis(false))
        assertEquals(3000L,AdvancedMixPolicy.holdMillis(true))
    }
    @Test fun thirdFailureLocksAndEvenTheCorrectCodeIsRejectedUntilTenMinutesPass() {
        var state = AdvancedMixPolicy.State()
        repeat(2) {
            val result = AdvancedMixPolicy.attempt(state, "wrong", 1000)
            assertEquals(AdvancedMixPolicy.Result.INCORRECT, result.result); state = result.state
        }
        val locked = AdvancedMixPolicy.attempt(state, "wrong", 1000)
        assertEquals(AdvancedMixPolicy.Result.LOCKED, locked.result)
        assertEquals(601000L, locked.state.lockedUntil)
        assertEquals(AdvancedMixPolicy.Result.LOCKED, AdvancedMixPolicy.attempt(locked.state, CODE, 600999, matches).result)
        assertEquals(AdvancedMixPolicy.Result.ENABLED, AdvancedMixPolicy.attempt(locked.state, CODE, 601000, matches).result)
    }
    @Test fun attemptsSurviveDialogRecreationAndSuccessfulEntryResetsThem() {
        val second = AdvancedMixPolicy.State(failures=2)
        val success = AdvancedMixPolicy.attempt(second, CODE, 2000, matches)
        assertEquals(AdvancedMixPolicy.State(), success.state)
        assertEquals(AdvancedMixPolicy.Result.LOCKED, AdvancedMixPolicy.attempt(second, CODE.uppercase(), 2000, matches).result)
    }
    @Test fun expiredLockStartsWithAFullThreeAttempts() {
        val expired = AdvancedMixPolicy.State(0, 1000)
        val attempt = AdvancedMixPolicy.attempt(expired, "", 1000)
        assertEquals(AdvancedMixPolicy.Result.INCORRECT, attempt.result)
        assertEquals(1, attempt.state.failures)
        assertEquals(0L, attempt.state.lockedUntil)
    }
}
