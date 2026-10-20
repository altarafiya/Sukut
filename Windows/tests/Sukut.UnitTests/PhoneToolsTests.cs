using Xunit;

namespace Sukut;
public sealed class PhoneToolsTests
{
    [Fact]
    public void BundleVersionAndUpdateDecisionsKeepNewerInstalls()
    {
        var version = PhoneTools.ParseSukutBundleVersion("0.3.30\n36\n");
        Assert.Equal("0.3.30", version.Version); Assert.Equal(36, version.Code);
        Assert.True(new SukutInstallation(35, 36, version.Version).CanUpdate);
        Assert.False(new SukutInstallation(null, 36, version.Version).CanUpdate);
        Assert.False(new SukutInstallation(36, 36, version.Version).CanUpdate);
        Assert.True(new SukutInstallation(37, 36, version.Version).IsCurrent);
    }
    [Theory]
    [InlineData("0.3.30\n")]
    [InlineData("0.3.30\n-1")]
    [InlineData("0.3.30\n36\nunexpected")]
    public void InvalidBundleMetadataCannotTriggerAnUpdate(string text)
        => Assert.Throws<System.IO.IOException>(() => PhoneTools.ParseSukutBundleVersion(text));
    [Fact]
    public void ParsesAuthorizationAndMultipleDevicesWithoutChoosingOne()
    {
        var devices = PhoneTools.ParseDevices("List of devices attached\r\nABC123 device product:phone model:Pixel_9 transport_id:1\r\nDEF456 unauthorized usb:1-2\n192.168.1.2:5555 offline\n");
        Assert.Equal(3, devices.Count);
        Assert.Equal("Pixel 9", devices[0].Model);
        Assert.Equal("unauthorized", devices[1].State);
        Assert.Equal("192.168.1.2:5555", devices[2].Serial);
        Assert.Equal("offline", devices[2].State);
    }
    [Fact]
    public void IgnoresDaemonMessagesAndMalformedSerials()
    {
        Assert.Empty(PhoneTools.ParseDevices("* daemon started successfully\nList of devices attached\nfoo;bar device model:Phone\n"));
    }
    [Theory]
    [InlineData("versionCode=1049 minSdk=23", 1049)]
    [InlineData("Packages:\n  versionCode=27 targetSdk=36", 27)]
    public void ReadsInstalledVersionWithoutMatchingOtherNumbers(string output, long expected)
        => Assert.Equal(expected, PhoneTools.ParseVersionCode(output));

    [Theory]
    [InlineData("Unable to find package: com.osama.sukut")]
    [InlineData("versionName=0.3.21")]
    [InlineData("versionCode=999999999999999999999999999")]
    public void MissingOrInvalidVersionIsNotTreatedAsInstalled(string output)
        => Assert.Null(PhoneTools.ParseVersionCode(output));
}
