using System.Text.Json.Serialization;

namespace Sukut;

public sealed class PresetTable
{
    public AudioModePreset off { get; set; } = AudioModePreset.ForMode(0);
    public AudioModePreset halal { get; set; } = AudioModePreset.ForMode(1);
    public AudioModePreset optimized { get; set; } = AudioModePreset.ForMode(2);
    public AudioModePreset strict { get; set; } = AudioModePreset.ForMode(3);
}

public sealed class PresetConfig
{
    public int activeMode { get; set; }
    public AudioModePreset activePreset { get; set; }
    public PresetTable presets { get; set; } = new();
    public static PresetConfig For(AudioModePreset preset) => new() { activeMode = preset.Mode, activePreset = preset };
}

[JsonSourceGenerationOptions(PropertyNameCaseInsensitive = true, WriteIndented = true)]
[JsonSerializable(typeof(EngineState))]
[JsonSerializable(typeof(List<SourceApp>))]
[JsonSerializable(typeof(UiPreferences))]
[JsonSerializable(typeof(PresetConfig))]
internal partial class SukutJsonContext : JsonSerializerContext { }
