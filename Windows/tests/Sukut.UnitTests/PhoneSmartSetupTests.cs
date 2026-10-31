using Xunit;
namespace Sukut;

public sealed class PhoneSmartSetupTests
{
    [Theory]
    [InlineData(null,false)] [InlineData(44L,true)] [InlineData(45L,null)] [InlineData(46L,null)]
    public async Task SmartSukutInstallsUpdatesOrKeepsCurrentAndNewerData(long? installed,bool? expectedUpdate)
    {
        long? code=installed;bool opened=false;List<bool> installs=[];
        await PhoneTools.SetupSukutCore(()=>Task.FromResult(new SukutInstallation(code,45,"0.4.0-beta.8")),
            update=>{installs.Add(update);code=45;return Task.FromResult("Success");},()=>{opened=true;return Task.FromResult("opened");});
        if(expectedUpdate==null)Assert.Empty(installs);else Assert.Equal(expectedUpdate.Value,Assert.Single(installs));
        Assert.True(opened);if(installed==46)Assert.Equal(46,code);
    }
    [Fact]
    public async Task SignatureFailureDoesNotOpenOrRemoveSukut()
    {
        bool opened=false;int calls=0;
        await Assert.ThrowsAsync<IOException>(()=>PhoneTools.SetupSukutCore(()=>Task.FromResult(new SukutInstallation(44,45,"0.4.0-beta.8")),
            _=>{calls++;throw new IOException("INSTALL_FAILED_UPDATE_INCOMPATIBLE");},()=>{opened=true;return Task.FromResult("");}));
        Assert.Equal(1,calls);Assert.False(opened);
    }
    [Fact]
    public async Task UnchangedVersionAfterInstallationDoesNotReportSuccess()
    {
        bool opened=false;
        await Assert.ThrowsAsync<IOException>(()=>PhoneTools.SetupSukutCore(()=>Task.FromResult(new SukutInstallation(44,45,"0.4.0-beta.8")),
            _=>Task.FromResult("Success"),()=>{opened=true;return Task.FromResult("");}));
        Assert.False(opened);
    }
}
