using System.Diagnostics;
using System.Text.Json.Serialization;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Storage;
using Windows.Storage.FileProperties;

namespace Sukut;

public sealed partial class SourceApp
{
    [JsonIgnore] public ImageSource? Icon { get; internal set; }
    [JsonIgnore] public string FallbackGlyph => Pid == 0 ? "\uE7F4" : "\uE737";
    [JsonIgnore] public Visibility IconVisibility => Icon == null ? Visibility.Collapsed : Visibility.Visible;
    [JsonIgnore] public Visibility FallbackVisibility => Icon == null ? Visibility.Visible : Visibility.Collapsed;
}

internal sealed class SourceAppIcons
{
    private const int Capacity = 48;
    private readonly Dictionary<string, ImageSource?> _cache = new(StringComparer.OrdinalIgnoreCase);
    private readonly Queue<string> _order = new();

    public async Task Populate(List<SourceApp> apps)
    {
        // Resolve process paths off the UI/native-engine threads. Cache by path,
        // not PID: Windows can reuse a PID after an app exits.
        var paths = await Task.Run(() => apps.Where(app => app.Pid != 0).Select(app =>
        {
            try
            {
                using var process = Process.GetProcessById(checked((int)app.Pid));
                return (App: app, Path: process.MainModule?.FileName);
            }
            catch { return (App: app, Path: (string?)null); }
        }).Where(item => !string.IsNullOrEmpty(item.Path)).ToArray());

        // Small batches avoid a burst of shell/file requests for many sessions.
        foreach (var batch in paths.Chunk(4))
            await Task.WhenAll(batch.Select(async item => item.App.Icon = await Load(item.Path!)));
    }

    private async Task<ImageSource?> Load(string path)
    {
        if (_cache.TryGetValue(path, out var existing)) return existing;
        ImageSource? icon = null;
        try
        {
            var file = await StorageFile.GetFileFromPathAsync(path);
            using var thumbnail = await file.GetThumbnailAsync(ThumbnailMode.SingleItem, 24, ThumbnailOptions.UseCurrentScale);
            if (thumbnail != null)
            {
                var bitmap = new BitmapImage { DecodePixelWidth = 24, DecodePixelHeight = 24 };
                await bitmap.SetSourceAsync(thumbnail);
                icon = bitmap;
            }
        }
        catch { /* Closed/protected apps keep their Fluent fallback icon. */ }
        if (!_cache.ContainsKey(path))
        {
            while (_cache.Count >= Capacity && _order.TryDequeue(out var oldest)) _cache.Remove(oldest);
            _order.Enqueue(path);
        }
        _cache[path] = icon;
        return icon;
    }
}
