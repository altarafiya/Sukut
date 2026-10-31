using Xunit;
namespace Sukut;

public sealed class PhoneWorkerTests
{
    [Fact] public void BrandPackageAndTwoPartReleaseVersionAreSupported()
    {
        Assert.Equal("com.altarafiyah.sukut",PhoneTools.SukutPackage);
        var version=PhoneTools.ParseSukutBundleVersion("0.4\n54\n");
        Assert.Equal("0.4",version.Version); Assert.Equal(54,version.Code);
        var info=PhoneTools.ParsePhoneInfo(new("test","device","Phone"),"","versionName=0.4\nversionCode=54\n",true);
        Assert.Equal("0.4",info.SukutVersion);
    }
    private const string Ready = "Result: Bundle[{state=READY, protocol=1, version=49, pid=1234}]";
    [Theory]
    [InlineData("Result: Bundle[{state=INACTIVE, protocol=1, version=49, pid=0}]")]
    [InlineData("Result: Bundle[{state=READY, protocol=2, version=49, pid=1234}]")]
    [InlineData("Result: Bundle[{state=READY, protocol=1, version=48, pid=1234}]")]
    [InlineData("Result: Bundle[{state=READY, protocol=1, version=490, pid=1234}]")]
    [InlineData("SUKUT_WORKER_LAUNCHED user=0")]
    [InlineData("Result: Bundle[{state=READY, protocol=1, version=49, pid=0}]")]
    public void AProcessOrWrongHandshakeIsNotReady(string output) => Assert.False(PhoneTools.WorkerIsReady(output,49));
    [Fact] public void CorrectHandshakeIsReady() => Assert.True(PhoneTools.WorkerIsReady(Ready,49));
    [Fact] public async Task RepeatedActivationDoesNotInterruptActiveWorker()
    {
        var calls=new List<string>();
        await PhoneTools.ActivateWorkerCore((args,_,_)=>{calls.Add(string.Join(" ",args));return Task.FromResult(Ready);},0,49,_=>Task.CompletedTask);
        Assert.Single(calls);Assert.DoesNotContain(calls,c=>c.Contains("shell sh")||c.Contains("deactivate")||c.Contains("kill"));
    }
    [Fact] public async Task LaunchIsVerifiedThroughProviderBeforeSuccess()
    {
        bool launched=false;int polls=0;
        await PhoneTools.ActivateWorkerCore((args,_,_)=>{
            string command=string.Join(" ",args);
            if(command.StartsWith("shell sh")){launched=true;return Task.FromResult("SUKUT_WORKER_LAUNCHED user=10");}
            polls++;Assert.Contains("--user 10",command);
            return Task.FromResult(launched&&polls>3?Ready:"INACTIVE");
        },10,49,_=>Task.CompletedTask);
        Assert.True(launched);Assert.Equal(4,polls);
    }
    [Fact] public async Task LaunchWithoutConnectionFailsWithBoundedDiagnostics()
    {
        int launches=0,polls=0;
        var error=await Assert.ThrowsAsync<IOException>(()=>PhoneTools.ActivateWorkerCore((args,_,_)=>{
            string command=string.Join(" ",args);
            if(command.StartsWith("shell sh")){launches++;return Task.FromResult("SUKUT_WORKER_LAUNCHED user=0");}
            if(command.StartsWith("shell tail"))return Task.FromResult("registerUidObserver failed");
            polls++;return Task.FromResult("INACTIVE");
        },0,49,_=>Task.CompletedTask));
        Assert.Equal(2,launches);Assert.Equal(25,polls);Assert.Contains("registerUidObserver failed",error.Message);
    }
    [Theory] [InlineData("-1")] [InlineData("1;reboot")] [InlineData("21475")] [InlineData("")]
    public void InvalidUserIsRejected(string text)=>Assert.Throws<IOException>(()=>PhoneTools.ParseUser(text));
    [Fact] public void PhoneCardUsesSukutVersionAndWorkerReadiness()
    {
        var info=PhoneTools.ParsePhoneInfo(new("private-serial","device","Phone"),
            "[ro.product.marketname]: [My Phone]\n[ro.build.version.release]: [16]\n","versionName=0.4.0-beta.12\nversionCode=49\n",true);
        Assert.True(info.WorkerReady);Assert.Equal(49,info.SukutCode);Assert.Equal("My Phone",info.Name);
        Assert.Equal("0.4.0-beta.12",info.SukutVersion);
        Assert.DoesNotContain("private-serial",info.ToString());
    }
}
