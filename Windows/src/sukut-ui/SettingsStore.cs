using System.Text.Json;

namespace Sukut;

internal sealed class SettingsStore : IAsyncDisposable
{
    private readonly string _path;
    private readonly SemaphoreSlim _write = new(1);
    private CancellationTokenSource? _pending;
    private Task _scheduled = Task.CompletedTask;
    private string? _lastJson;
    private long _version;
    private bool _disposed;
    internal event Action<Exception>? Failed;

    internal SettingsStore(string path) => _path = path;

    internal void Schedule(UiPreferences preferences)
    {
        if (_disposed) return;
        var snapshot = preferences.Copy();
        var cancellation = new CancellationTokenSource();
        var token = cancellation.Token;
        var old = Interlocked.Exchange(ref _pending, cancellation);
        old?.Cancel(); old?.Dispose();
        long version = Interlocked.Increment(ref _version);
        _scheduled = Task.Run(async () =>
        {
            try
            {
                await Task.Delay(300, token);
                await Write(snapshot, version, token);
            }
            catch (OperationCanceledException) { }
            catch (Exception error) { Failed?.Invoke(error); }
        });
    }

    internal async Task Flush(UiPreferences preferences)
    {
        _pending?.Cancel();
        var snapshot = preferences.Copy();
        long version = Interlocked.Increment(ref _version);
        await Task.Run(() => Write(snapshot, version, CancellationToken.None));
    }

    private async Task Write(UiPreferences preferences, long version, CancellationToken cancellation)
    {
        string json = JsonSerializer.Serialize(preferences, SukutJsonContext.Default.UiPreferences);
        await _write.WaitAsync(cancellation);
        try
        {
            if (version != Volatile.Read(ref _version) || json == _lastJson) return;
            Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
            string temporary = _path + ".tmp";
            // Once replacement begins, finish it atomically even if a newer edit arrives.
            await File.WriteAllTextAsync(temporary, json);
            File.Move(temporary, _path, true);
            _lastJson = json;
        }
        finally { _write.Release(); }
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        _pending?.Cancel();
        await _scheduled;
        _pending?.Dispose();
        _write.Dispose();
    }
}
