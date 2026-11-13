using System.Security.Cryptography;
using System.Text;

namespace Sukut;

// Invoked only by the hidden dialog; there is no countdown or background poll.
internal sealed class AdvancedMixAccess
{
    internal const int HoldMilliseconds = 5000;
    internal const int DisableHoldMilliseconds = 3000;
    internal static int HoldDuration(bool enabled) => enabled ? DisableHoldMilliseconds : HoldMilliseconds;
    internal const long LockMilliseconds = 600000;
    internal int Failures { get; private set; }
    internal long LockedUntil { get; private set; }
    internal enum Result { Enabled, Incorrect, Locked }
    // Only a salted SHA-256 of the access code is in the source.
    private const string CodeSha256 = "1eab90bc571f33625f026854d109a8a8331d688bdddd31c1917f7631e49c6d60";
    internal static bool IsAccessCode(string code) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("sukut-advanced-mix:" + code))).ToLowerInvariant() == CodeSha256;
    internal Result Attempt(string code, long now, Func<string, bool>? matches = null)
    {
        if (LockedUntil > now) return Result.Locked;
        if (LockedUntil != 0) { LockedUntil = 0; Failures = 0; }
        if (matches != null ? matches(code) : IsAccessCode(code)) { Failures = 0; return Result.Enabled; }
        if (++Failures >= 3) { Failures = 0; LockedUntil = now + LockMilliseconds; return Result.Locked; }
        return Result.Incorrect;
    }
    internal static AdvancedMixAccess Load(string path)
    {
        var state = new AdvancedMixAccess();
        try
        {
            var fields = File.ReadAllText(path).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
            if (fields.Length == 2 && int.TryParse(fields[0], out int failed) && long.TryParse(fields[1], out long until))
            { state.Failures = Math.Clamp(failed, 0, 2); state.LockedUntil = Math.Max(0, until); }
        }
        catch (IOException) { }
        return state;
    }
    internal async Task Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllTextAsync(path + ".tmp", $"{Failures} {LockedUntil}\n");
        File.Move(path + ".tmp", path, true);
    }
}
