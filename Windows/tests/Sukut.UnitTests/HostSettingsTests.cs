using System.Text.Json;
using Sukut;
using Xunit;

public sealed class HostSettingsTests
{
    [Fact]
    public void TrayChangesAdoptTogetherWithoutResettingAppearanceOrSetup()
    {
        var preferences = new UiPreferences { Output = "speakers", Language = "ar", Theme = "dark", SetupCompleted = true };
        var revisions = new HostSettingsRevision();
        var json = "{\"hostSettingsVersion\":4,\"hostOutput\":\"headphones\",\"hostBuffering\":2,\"hostSourcePid\":42,\"hostOptimizedBackgroundDb\":-12}";
        var state = JsonSerializer.Deserialize(json, SukutJsonContext.Default.EngineState)!;
        Assert.True(revisions.TryAdopt(state, preferences));
        Assert.Equal("headphones", preferences.Output); Assert.Equal(2, preferences.Buffering); Assert.Equal(42u, preferences.SourcePid);
        Assert.Equal(-12,preferences.OptimizedBackgroundDb);
        Assert.Equal("ar", preferences.Language); Assert.Equal("dark", preferences.Theme); Assert.True(preferences.SetupCompleted);
        var saved = JsonSerializer.Deserialize(JsonSerializer.Serialize(preferences.Copy(), SukutJsonContext.Default.UiPreferences), SukutJsonContext.Default.UiPreferences)!;
        Assert.Equal(4, saved.HostSettingsVersion);
        Assert.Equal(-12,saved.OptimizedBackgroundDb);
    }

    [Fact]
    public void RepeatedAndOlderPollsDoNotUndoLaterOffModeEdits()
    {
        var preferences = new UiPreferences { OptimizedBackgroundDb = -7 }; var revisions = new HostSettingsRevision();
        var state = new EngineState { HostSettingsVersion = 2, HostOutput = "headphones", HostBuffering = 1 };
        Assert.True(revisions.TryAdopt(state, preferences));
        Assert.Equal(-7,preferences.OptimizedBackgroundDb); // Older hosts omit this field.
        preferences.Buffering = 2; preferences.SourcePid = 100;
        Assert.False(revisions.TryAdopt(state, preferences));
        Assert.False(revisions.TryAdopt(new EngineState { HostSettingsVersion = 1, HostOutput = "old-speakers" }, preferences));
        Assert.Equal("headphones", preferences.Output); Assert.Equal(2, preferences.Buffering); Assert.Equal(100u, preferences.SourcePid);
    }

    [Fact]
    public void InFlightSwitchIsAppliedOnlyAfterHostCompletes()
    {
        var preferences = new UiPreferences { Output = "speakers" }; var revisions = new HostSettingsRevision();
        var state = new EngineState { HostCommand = 1, HostSettingsVersion = 3, HostOutput = "headphones" };
        Assert.False(revisions.TryAdopt(state, preferences)); Assert.Equal("speakers", preferences.Output);
        state.HostCommand = 0;
        Assert.True(revisions.TryAdopt(state, preferences)); Assert.Equal("headphones", preferences.Output);
    }
}
