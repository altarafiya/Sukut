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

public sealed class Engine : IAsyncDisposable
{
    private IntPtr _context;
    private string _root = "";
    private readonly BlockingCollection<Action> _jobs = new();
    private readonly Thread _nativeThread;

    private readonly NativeReplyBuffer _reply = new();
    private readonly EndpointSnapshotCache _endpointCache = new();

    public Engine()
    {
        _nativeThread = new Thread(() =>
        {
            foreach (var job in _jobs.GetConsumingEnumerable())
            {
                job();
            }
        })
        {
            IsBackground = true,
            Name = "Sukut native controller"
        };
        _nativeThread.SetApartmentState(ApartmentState.STA);
        _nativeThread.Start();
    }

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern IntPtr Sukut_Create(string root);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_Read(IntPtr h, byte[] output, int capacity);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_ReadSince(IntPtr h, long endpointsVersion, byte[] output, int capacity);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_Apps(byte[] output, int capacity);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_Error(byte[] output, int capacity);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
    private static extern int Sukut_SelectPreset(IntPtr h, int mode, string source, string capture, string output, uint pid,
        float voiceDb, float musicDb, float backgroundDb);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Sukut_Refresh(IntPtr h);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_SetBuffering(IntPtr h, int level);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_SetAmplification(IntPtr h, int db);
    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern int Sukut_SetCablePassthroughConfirmed(IntPtr h, int confirmed);

    [DllImport("SukutEngine.dll", CallingConvention = CallingConvention.Cdecl)]
    private static extern void Sukut_Destroy(IntPtr h);

    private string Text(Func<byte[], int, int> call) => Encoding.UTF8.GetString(_reply.Read(call, Sukut_Error));

    private Task<T> Work<T>(Func<T> action)
    {
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        _jobs.Add(() =>
        {
            try
            {
                completion.SetResult(action());
            }
            catch (Exception e)
            {
                completion.SetException(e);
            }
        });
        return completion.Task;
    }

    private void WritePresetConfig(AudioModePreset activePreset)
    {
        if (string.IsNullOrEmpty(_root)) return;
        try
        {
            var runtimeDir = Path.Combine(_root, "runtime");
            Directory.CreateDirectory(runtimeDir);
            var presetFile = Path.Combine(runtimeDir, "sukut-presets.json");
            var payload = PresetConfig.For(activePreset);
            File.WriteAllText(presetFile, JsonSerializer.Serialize(payload, SukutJsonContext.Default.PresetConfig));
        }
        catch
        {
            // Ignore non-critical file write errors so audio routing continues uninterrupted
        }
    }

    public Task Initialize(string root) => Work(() =>
    {
        _root = root;
        WritePresetConfig(AudioModePreset.ForMode(0));
        _context = Sukut_Create(root);
        if (_context == IntPtr.Zero)
            throw new InvalidOperationException(Text(Sukut_Error));
        return true;
    });

    private EngineState ReadState() => _endpointCache.Adopt(JsonSerializer.Deserialize(
        _reply.Read((b, n) => Sukut_ReadSince(_context, _endpointCache.Version, b, n), Sukut_Error), SukutJsonContext.Default.EngineState)!);
    public Task<EngineState> Read() => Work(ReadState);

    public Task<EngineState> RefreshDevices() => Work(() =>
    {
        var before = ReadState();
        Sukut_Refresh(_context);
        var until = Environment.TickCount64 + 3000;
        do
        {
            var state = ReadState();
            if (state.EndpointsVersion > before.EndpointsVersion) return state;
            if (state.Error != before.Error && !string.IsNullOrWhiteSpace(state.Error)) throw new InvalidOperationException(state.Error);
            Thread.Sleep(20);
        } while (Environment.TickCount64 < until);
        throw new InvalidOperationException("Audio device refresh timed out. Try opening the list again.");
    });

    public Task<List<SourceApp>> Apps() => Work(() =>
        JsonSerializer.Deserialize(_reply.Read(Sukut_Apps, Sukut_Error), SukutJsonContext.Default.ListSourceApp)!);

    public Task Select(int mode, string source, string capture, string output, uint pid, int buffering = 0, bool cablePassthroughConfirmed = false, int optimizedBackgroundDb = 0, bool restart = false) => Work(() =>
    {
        if (Sukut_SetCablePassthroughConfirmed(_context, cablePassthroughConfirmed ? 1 : 0) == 0)
            throw new InvalidOperationException(Text(Sukut_Error));
        if (Sukut_SetBuffering(_context, buffering) == 0) throw new InvalidOperationException(Text(Sukut_Error));
        var preset = AudioModePreset.ForMode(mode, optimizedBackgroundDb);
        // Same-preset selection changes its mix; Restart explicitly recreates clients.
        if (restart && mode != 0 && Sukut_SelectPreset(_context, 0, source, capture, output, pid, 0, 0, 0) == 0)
            throw new InvalidOperationException(Text(Sukut_Error));
#if DEBUG
        if (Environment.GetEnvironmentVariable("SUKUT_TRACE_NATIVE") == "1")
        {
            string F(float value) => value.ToString(System.Globalization.CultureInfo.InvariantCulture);
            File.AppendAllText(Path.Combine(_root, "runtime", "sukut-native-arguments.tsv"),
                $"{mode}\t{source}\t{capture}\t{output}\t{pid}\t{F(preset.VoiceDb)}\t{F(preset.MuteMusic ? -100f : preset.MusicDb)}\t{F(preset.MuteBackground ? -100f : preset.BackgroundDb)}{Environment.NewLine}");
        }
#endif
        // Publish routing, mode and all stem gains together, before audio starts.
        if (Sukut_SelectPreset(_context, mode, source, capture, output, pid, preset.VoiceDb,
            preset.MuteMusic ? -100.0f : preset.MusicDb,
            preset.MuteBackground ? -100.0f : preset.BackgroundDb) == 0)
            throw new InvalidOperationException(Text(Sukut_Error));
        WritePresetConfig(preset);
        return true;
    });

    public Task Refresh() => Work(() =>
    {
        Sukut_Refresh(_context);
        return true;
    });

    public Task SetAmplification(int db) => Work(() =>
    {
        if (Sukut_SetAmplification(_context, db) == 0) throw new InvalidOperationException(Text(Sukut_Error));
        return true;
    });

    public async ValueTask DisposeAsync()
    {
        await Work(() =>
        {
            if (_context != IntPtr.Zero)
            {
                Sukut_Destroy(_context);
                _context = IntPtr.Zero;
            }
            return true;
        });
        _jobs.CompleteAdding();
        await Task.Run(() => _nativeThread.Join());
        _jobs.Dispose();
    }
}

