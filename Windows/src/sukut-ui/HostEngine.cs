using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;

namespace Sukut;

/// <summary>
/// The window's connection to the always-running native host (Sukut.exe),
/// which owns the audio engine. Same calls as the old in-process bridge, sent
/// over a per-session named pipe. Closing the window only disconnects: the
/// host keeps filtering and supervising from the tray.
/// </summary>
public sealed class Engine : IAsyncDisposable
{
    private const char Separator = '\x1f';
    private string _root = "";
    private NamedPipeClientStream? _pipe;
    private readonly BlockingCollection<Action> _jobs = new();
    private readonly Thread _worker;
    private readonly EndpointSnapshotCache _endpointCache = new();
    private readonly byte[] _chunk = new byte[64 * 1024];

    public Engine()
    {
        _worker = new Thread(() => { foreach (var job in _jobs.GetConsumingEnumerable()) job(); })
        {
            IsBackground = true,
            Name = "Sukut host connection"
        };
        _worker.Start();
    }

    private static string PipeName => "Sukut.Host." + Process.GetCurrentProcess().SessionId;

    private Task<T> Work<T>(Func<T> action)
    {
        var completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        _jobs.Add(() =>
        {
            try { completion.SetResult(action()); }
            catch (Exception e) { completion.SetException(e); }
        });
        return completion.Task;
    }

    private string Request(params string[] fields)
    {
        var pipe = _pipe ?? throw new InvalidOperationException("Sukut is not running.");
        try
        {
            var payload = Encoding.UTF8.GetBytes(string.Join(Separator, fields));
            pipe.Write(payload, 0, payload.Length);
            pipe.Flush();
            using var reply = new MemoryStream();
            do
            {
                int read = pipe.Read(_chunk, 0, _chunk.Length);
                if (read == 0) throw new IOException("closed");
                reply.Write(_chunk, 0, read);
            } while (!pipe.IsMessageComplete);
            var bytes = reply.GetBuffer();
            int length = (int)reply.Length;
            if (length < 1) throw new IOException("empty reply");
            string text = Encoding.UTF8.GetString(bytes, 1, length - 1);
            return bytes[0] == (byte)'1' ? text : throw new InvalidOperationException(text);
        }
        catch (IOException)
        {
            throw new InvalidOperationException("Sukut stopped running. Open Sukut again.");
        }
    }

    // The host lives beside the window (MSIX) or one folder up (portable).
    private static string? HostExecutable()
    {
        foreach (var candidate in new[] { Path.Combine(AppContext.BaseDirectory, "Sukut.exe"), Path.Combine(AppContext.BaseDirectory, "..", "Sukut.exe") })
            if (File.Exists(candidate)) return Path.GetFullPath(candidate);
        return null;
    }

    private void Connect()
    {
        var deadline = Environment.TickCount64 + 20000;
        bool started = false;
        while (true)
        {
            var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.None);
            try
            {
                pipe.Connect(500);
                pipe.ReadMode = PipeTransmissionMode.Message;
                _pipe = pipe;
                return;
            }
            catch (TimeoutException) { pipe.Dispose(); }
            catch (IOException) { pipe.Dispose(); Thread.Sleep(100); }
            // Opened directly (not from the tray host): start the host quietly.
            if (!started && !Mutex.TryOpenExisting(@"Local\SukutHost", out _))
            {
                started = true;
                var host = HostExecutable() ?? throw new InvalidOperationException("Sukut.exe is missing. Keep the app folder beside Sukut.exe.");
                Process.Start(new ProcessStartInfo(host, "--no-window") { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(host)! });
            }
            if (Environment.TickCount64 > deadline) throw new InvalidOperationException("Sukut's audio engine did not respond. Close Sukut from the tray and open it again.");
        }
    }

    public Task Initialize(string root, string? resourceRoot = null, IntPtr window = default) => Work(() =>
    {
        _root = root;
        Connect();
        // The host replies once its engine is ready, or with the start-up error.
        Request("hello", window.ToInt64().ToString(System.Globalization.CultureInfo.InvariantCulture), Environment.ProcessId.ToString(System.Globalization.CultureInfo.InvariantCulture));
        return true;
    });

    private EngineState ReadState() => _endpointCache.Adopt(JsonSerializer.Deserialize(
        Request("read", _endpointCache.Version.ToString(System.Globalization.CultureInfo.InvariantCulture)), SukutJsonContext.Default.EngineState)!);
    public Task<EngineState> Read() => Work(ReadState);

    public Task<EngineState> RefreshDevices() => Work(() =>
    {
        var before = ReadState();
        Request("refresh");
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
        JsonSerializer.Deserialize(Request("apps"), SukutJsonContext.Default.ListSourceApp)!);

    public Task Select(int mode, string source, string capture, string output, uint pid, int buffering = 0, bool cablePassthroughConfirmed = false, int optimizedBackgroundDb = 0, bool restart = false) => Work(() =>
    {
        string I(long value) => value.ToString(System.Globalization.CultureInfo.InvariantCulture);
        // The host applies the same stem gains as AudioModePreset, publishing
        // routing, mode and mix together before audio starts.
        Request("select", I(mode), source, capture, output, I(pid), I(Math.Clamp(buffering, 0, 2)),
            cablePassthroughConfirmed ? "1" : "0", I(Math.Clamp(optimizedBackgroundDb, -20, 10)), restart ? "1" : "0");
        WritePresetConfig(AudioModePreset.ForMode(mode, optimizedBackgroundDb));
        return true;
    });

    private void WritePresetConfig(AudioModePreset activePreset)
    {
        if (string.IsNullOrEmpty(_root)) return;
        try
        {
            var runtimeDir = Path.Combine(_root, "runtime");
            Directory.CreateDirectory(runtimeDir);
            File.WriteAllText(Path.Combine(runtimeDir, "sukut-presets.json"),
                JsonSerializer.Serialize(PresetConfig.For(activePreset), SukutJsonContext.Default.PresetConfig));
        }
        catch
        {
            // Diagnostics only; never interrupt audio routing.
        }
    }

    public Task Refresh() => Work(() => { Request("refresh"); return true; });

    public Task SetAmplification(int db) => Work(() => { Request("amp", Math.Clamp(db, 0, 12).ToString(System.Globalization.CultureInfo.InvariantCulture)); return true; });

    /// <summary>Exit Sukut entirely: the host restores audio routes and quits.</summary>
    public Task ExitHost() => Work(() => { Request("exit"); return true; });

    public async ValueTask DisposeAsync()
    {
        await Work(() => { _pipe?.Dispose(); _pipe = null; return true; });
        _jobs.CompleteAdding();
        await Task.Run(() => _worker.Join());
        _jobs.Dispose();
    }
}
