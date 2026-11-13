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

    [Theory]
    [InlineData(null,true)]
    [InlineData(48L,true)]
    [InlineData(49L,false)]
    [InlineData(56L,false)]
    [InlineData(58L,false)]
    [InlineData(99L,false)]
    public void ActivationKeepsSupportedInstalledVersions(long? code,bool needsInstall)
        => Assert.Equal(needsInstall,PhoneTools.NeedsActivationInstall(code));
    private const string Ready58 = "Result: Bundle[{state=READY, protocol=1, version=58, pid=1234}]";
    private const string ShortcutReady = "Result: Bundle[{state=COMPUTER_SETUP_READY}]";
    [Fact] public async Task OlderPhoneDoesNotReceiveShortcutCommands()
    {
        var result = await PhoneTools.ConfigureComputerShortcutCore((_,_,_)=>throw new InvalidOperationException("No command expected"),0,57);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.NotSupported,result);
    }
    [Fact] public async Task ShortcutGrantFollowsReadyAndUsesTheCurrentUser()
    {
        var calls = new List<string>();
        var result = await PhoneTools.ConfigureComputerShortcutCore((args,seconds,allowFailure)=>{
            string command = string.Join(" ",args); calls.Add(command);
            Assert.Equal(15,seconds);
            if (args[^1] == "status") return Task.FromResult(Ready58);
            if (args[1] == "pm") { Assert.False(allowFailure); return Task.FromResult(""); }
            Assert.True(allowFailure); return Task.FromResult(ShortcutReady);
        },10,58);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.Ready,result);
        Assert.Equal(3,calls.Count);
        Assert.EndsWith("--method status",calls[0]);
        Assert.Equal("shell pm grant --user 10 com.altarafiyah.sukut android.permission.WRITE_SECURE_SETTINGS",calls[1]);
        Assert.EndsWith("--method computer-setup",calls[2]);
        Assert.All(calls,c=>Assert.Contains("--user 10",c));
        Assert.DoesNotContain(calls,c=>c.Contains("settings put")||c.Contains("deactivate")||c.Contains("kill"));
    }
    [Fact] public async Task AWorkerThatDiedIsNotProvisioned()
    {
        int calls = 0;
        var result = await PhoneTools.ConfigureComputerShortcutCore((args,_,_)=>{
            calls++; Assert.Equal("status",args[^1]); return Task.FromResult("INACTIVE");
        },0,58);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.WorkerNotReady,result); Assert.Equal(1,calls);
    }
    [Theory]
    [InlineData("Result: Bundle[{state=COMPUTER_SETUP_READY_EXTRA}]")]
    [InlineData("Result: Bundle[{state=INACTIVE}]")]
    [InlineData("SecurityException: Computer shortcut permission was not granted")]
    public async Task GrantAloneDoesNotClaimShortcutSuccess(string acknowledgement)
    {
        var result = await PhoneTools.ConfigureComputerShortcutCore((args,_,_)=>Task.FromResult(
            args[^1] == "status" ? Ready58 : args[^1] == "computer-setup" ? acknowledgement : ""),0,58);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.Unavailable,result);
    }
    [Fact] public async Task RejectedGrantPreservesSuccessfulAudioActivation()
    {
        int calls = 0;
        var result = await PhoneTools.ConfigureComputerShortcutCore((args,_,_)=>{
            calls++;
            if (args[^1] == "status") return Task.FromResult(Ready58);
            throw new IOException("Permission denied");
        },0,58);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.Unavailable,result); Assert.Equal(2,calls);
    }
    [Fact] public async Task ShortcutTimeoutIsNonfatal()
    {
        var result = await PhoneTools.ConfigureComputerShortcutCore((args,_,_)=>{
            if (args[^1] == "status") return Task.FromResult(Ready58);
            throw new OperationCanceledException();
        },0,58);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.Unavailable,result);
    }
    [Fact] public async Task AlreadyReadyWorkerCanReceiveShortcutWithoutAnotherLaunch()
    {
        var calls = new List<string>();
        Task<string> Run(string[] args,int seconds,bool allowFailure)
        {
            calls.Add(string.Join(" ",args));
            return Task.FromResult(args[^1] == "computer-setup" ? ShortcutReady : args[1] == "pm" ? "" : Ready58);
        }
        await PhoneTools.ActivateWorkerCore(Run,0,58,_=>Task.CompletedTask);
        Assert.Equal(PhoneTools.ComputerShortcutSetup.Ready,await PhoneTools.ConfigureComputerShortcutCore(Run,0,58));
        Assert.Equal(4,calls.Count);
        Assert.DoesNotContain(calls,c=>c.StartsWith("shell sh")||c.Contains("deactivate")||c.Contains("kill"));
    }
}
