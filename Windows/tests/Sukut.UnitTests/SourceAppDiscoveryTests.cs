using Xunit;

namespace Sukut.Tests;

public class SourceAppDiscoveryTests
{
    [Fact]
    public void KeepsAudioOnlyAppsAndWindowsThatHaveNotPlayedYet()
    {
        var apps = SourceAppDiscovery.Merge([new() { Pid = 12, Name = "Audio player" }], [new() { Pid = 18, Name = "Browser" }]);
        Assert.Equal(new uint[] { 12, 18 }, apps.Select(app => app.Pid));
    }

    [Fact]
    public void AudioSessionNameWinsWithoutRepeatingTheSameProcess()
    {
        var apps = SourceAppDiscovery.Merge([new() { Pid = 12, Name = "Friendly app" }], [new() { Pid = 12, Name = "app" }]);
        Assert.Equal("Friendly app", Assert.Single(apps).Name);
    }

    [Fact]
    public void SeparateAppProcessesRemainSelectableAndSystemEntryIsNotDuplicated()
    {
        var apps = SourceAppDiscovery.Merge([new() { Pid = 0, Name = "System" }], [new() { Pid = 22, Name = "Browser" }, new() { Pid = 23, Name = "Browser" }]);
        Assert.Equal(2, apps.Count);
        Assert.DoesNotContain(apps, app => app.Pid == 0);
    }

    [Fact]
    public void SystemInputAndAutomationHelpersDoNotBecomeSourceCards()
    {
        var apps = SourceAppDiscovery.Merge([new() { Pid = 2, Name = "codex-computer-use" }],
            [new() { Pid = 3, Name = "TextInputHost" }, new() { Pid = 4, Name = "Browser" }]);
        Assert.Equal("Browser", Assert.Single(apps).Name);
    }
}
