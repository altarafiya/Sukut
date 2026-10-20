package com.osama.cleanwave

/** Advice needs repeated trouble during progressing playback, not a single seek/start burst. */
internal class PlaybackAdvicePolicy {
    private var initialized = false
    private var graceUntil = 0L
    private var previousTime = 0L
    private var previousPackets = 0L
    private var previousFallbacks = 0L
    private var previousUnderruns = 0L
    private var lastFallbackTime = Long.MIN_VALUE / 2
    private var lastTrouble = 0L
    private var suggested = false
    private val episodes = java.util.ArrayDeque<Long>()
    private val underrunWindows = java.util.ArrayDeque<Long>()

    fun reset() {
        initialized = false; suggested = false; episodes.clear(); underrunWindows.clear()
        lastFallbackTime = Long.MIN_VALUE / 2; lastTrouble = 0
    }
    fun observe(now: Long, packets: Long, fallbacks: Long, underruns: Long): Boolean {
        val reset = !initialized || packets < previousPackets || fallbacks < previousFallbacks || underruns < previousUnderruns
        if (reset) {
            episodes.clear(); underrunWindows.clear(); suggested = false
            graceUntil = now + 12_000; initialized = true
        } else if (packets == previousPackets || now - previousTime > 3_500) {
            // No captured audio / a suspended polling loop is not sustained overload.
            graceUntil = now + 3_000; episodes.clear(); underrunWindows.clear()
        } else if (now >= graceUntil) {
            if (fallbacks > previousFallbacks && now - lastFallbackTime >= 2_000) {
                if (episodes.size >= 8) episodes.removeFirst(); episodes.addLast(now); lastFallbackTime = now; lastTrouble = now
            }
            if (underruns > previousUnderruns) {
                if (underrunWindows.size >= 32) underrunWindows.removeFirst(); underrunWindows.addLast(now); lastTrouble = now
            }
        }
        while (episodes.isNotEmpty() && now - episodes.first() > 45_000) episodes.removeFirst()
        while (underrunWindows.isNotEmpty() && now - underrunWindows.first() > 30_000) underrunWindows.removeFirst()
        // Only advise after sustained trouble, never for a single seek, start, or track change.
        if (episodes.size >= 5 || (underrunWindows.size >= 12 && now - underrunWindows.first() >= 20_000)) suggested = true
        if (suggested && now - lastTrouble > 45_000) suggested = false
        previousTime = now; previousPackets = packets; previousFallbacks = fallbacks; previousUnderruns = underruns
        return suggested
    }
}
