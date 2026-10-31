using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace Sukut;

/// <summary>
/// Represents the stem gain configuration for each audio filtering mode.
/// Mode 0 (OFF):       Music  0 dB, Background  0 dB, Voice  0 dB
/// Mode 1 (Music Filter): Music Muted, Background 0 dB, Voice 0 dB
/// Mode 2 (Optimized): Music muted, adjustable background, Voice 0 dB
/// Mode 3 (Strict):    Music Muted, Background Muted, Voice 0 dB
/// </summary>
public readonly record struct AudioModePreset(
    int Mode,
    string Name,
    float MusicDb,
    float BackgroundDb,
    float VoiceDb,
    bool MuteMusic,
    bool MuteBackground)
{
    public float MusicLinear => MuteMusic ? 0.0f : DbToLinear(MusicDb);
    public float BackgroundLinear => MuteBackground ? 0.0f : DbToLinear(BackgroundDb);
    public float VoiceLinear => DbToLinear(VoiceDb);

    public static float DbToLinear(float db) => db <= -80.0f ? 0.0f : MathF.Pow(10.0f, db / 20.0f);

    public static AudioModePreset ForMode(int mode, int optimizedBackgroundDb = 0) => mode switch
    {
        // Music Filter keeps voice and background at unity.
        1 => new AudioModePreset(
            Mode: 1,
            Name: "Music Filter",
            MusicDb: -80.0f,
            BackgroundDb: 0.0f,
            VoiceDb: 0.0f,
            MuteMusic: true,
            MuteBackground: false),

        3 => new AudioModePreset(
            Mode: 3,
            Name: "Strict",
            MusicDb: -80.0f,
            BackgroundDb: -80.0f,
            VoiceDb: 0.0f,
            MuteMusic: true,
            MuteBackground: true),

        // The saved background level is independent of the other presets.
        2 => new AudioModePreset(
            Mode: 2,
            Name: "Optimized",
            MusicDb: -100.0f,
            BackgroundDb: Math.Clamp(optimizedBackgroundDb, -20, 10),
            VoiceDb: 0.0f,
            MuteMusic: true,
            MuteBackground: false),

        // OFF stops the bridge and restores the original route.
        _ => new AudioModePreset(
            Mode: 0,
            Name: "Off",
            MusicDb: 0.0f,
            BackgroundDb: 0.0f,
            VoiceDb: 0.0f,
            MuteMusic: false,
            MuteBackground: false)
    };
}

public sealed class Endpoint
{
    public string Id { get; set; } = "";
    private string _name = "", _glyph = "";
    public string Name { get => _name; set { _name = value; _glyph = ""; } }
    public int Flow { get; set; }
    public int Virtual { get; set; }

    /// <summary>
    /// Maps Windows audio endpoint names to Fluent Segoe Icons.
    /// </summary>
    public string Glyph
    {
        get => _glyph.Length == 0 ? _glyph = ResolveGlyph() : _glyph;
    }
    private string ResolveGlyph()
    {
            if (Name.Contains("headset", StringComparison.OrdinalIgnoreCase))
                return "\uE95B"; // Headset icon
            if (Name.Contains("head", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("HyperX", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("earphone", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("buds", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("AirPods", StringComparison.OrdinalIgnoreCase))
                return "\uE7F6"; // Headphones icon
            if (Name.Contains("HDMI", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("DisplayPort", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("NVIDIA", StringComparison.OrdinalIgnoreCase) ||
                Name.Contains("Monitor", StringComparison.OrdinalIgnoreCase))
                return "\uE7F4"; // Monitor / TV audio icon
            return "\uE767";     // Default speaker volume icon
    }
}

public sealed partial class SourceApp
{
    public uint Pid { get; set; }
    public string Name { get; set; } = "";
}

public sealed class EngineState
{
    public int Initialized { get; set; }
    public int Busy { get; set; }
    public int State { get; set; }
    public int Paused { get; set; }
    public string Error { get; set; } = "";
    public string Output { get; set; } = "";
    public string Source { get; set; } = "";
    public string Capture { get; set; } = "";
    public long EndpointsVersion { get; set; }
    public long Session { get; set; }
    public long Fallbacks { get; set; }
    public long Recoveries { get; set; }
    public long Bypasses { get; set; }
    public long CoveredOverruns { get; set; }
    public int PipelineActive { get; set; }
    public long ControllerAgeMs { get; set; }
    public double ProcessingMeanMs { get; set; }
    public double ProcessingMaxMs { get; set; }
    // Standby: nothing is playing, so the AI rests until sound returns.
    public int Standby { get; set; }
    public string Kernels { get; set; } = "";
    // Mode the host is applying (also changed from the tray flyout); -1 if unknown.
    public int Mode { get; set; } = -1;
    public int HostCommand { get; set; }
    public long HostSettingsVersion { get; set; }
    public string HostOutput { get; set; } = "";
    public int HostBuffering { get; set; }
    public uint HostSourcePid { get; set; }
    public int? HostOptimizedBackgroundDb { get; set; }
    public List<Endpoint> Endpoints { get; set; } = [];
}

// Native polls omit endpoints while the enumeration revision is unchanged.
// Keep the same list/objects for the UI; a new revision (even empty) replaces it.
internal sealed class EndpointSnapshotCache
{
    internal long Version { get; private set; } = -1;
    private List<Endpoint> _endpoints = [];
    internal EngineState Adopt(EngineState state)
    {
        if (state.EndpointsVersion == Version) state.Endpoints = _endpoints;
        else { Version = state.EndpointsVersion; _endpoints = state.Endpoints; }
        return state;
    }
}

// A newer host revision is an applied routing change, not just a fresh poll.
// Duplicate polls must leave subsequent local (Off-mode) edits intact.
internal sealed class HostSettingsRevision
{
    internal long Version { get; private set; }
    internal bool TryAdopt(EngineState state, UiPreferences preferences)
    {
        if (state.HostCommand != 0 || state.HostSettingsVersion <= Version) return false;
        Version = state.HostSettingsVersion;
        if (!string.IsNullOrEmpty(state.HostOutput)) preferences.Output = state.HostOutput;
        preferences.Buffering = Math.Clamp(state.HostBuffering, 0, 2);
        preferences.SourcePid = state.HostSourcePid;
        if (state.HostOptimizedBackgroundDb is int background) preferences.OptimizedBackgroundDb = Math.Clamp(background, -20, 10);
        preferences.HostSettingsVersion = Version;
        return true;
    }
}

