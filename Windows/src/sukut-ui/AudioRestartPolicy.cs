namespace Sukut;

// One retry budget survives short-lived sessions, so intermittent faults cannot
// create an endless restart loop. A minute of healthy playback replenishes it.
internal sealed class AudioRestartPolicy
{
    internal const int MaximumAttempts = 3;
    internal const long HealthyResetMs = 60000;
    private long? _healthySince;
    internal int Attempts { get; private set; }
    internal long? DueAt { get; private set; }
    internal bool Pending => DueAt.HasValue;
    internal bool Request(long now)
    {
        _healthySince = null;
        if (Pending) return true;
        if (Attempts >= MaximumAttempts) return false;
        DueAt = now + (1000L << Attempts);
        return true;
    }
    internal bool TryBegin(long now)
    {
        if (!DueAt.HasValue || now < DueAt.Value) return false;
        DueAt = null; ++Attempts; return true;
    }
    internal void Healthy(long now)
    {
        DueAt = null;
        _healthySince ??= now;
        if (now - _healthySince.Value >= HealthyResetMs) Reset();
    }
    internal void Reset() { Attempts = 0; DueAt = null; _healthySince = null; }
}
