using Xunit;

namespace Sukut;

public sealed class AdvancedMixTests
{
    // The real code is not in the source; the policy is tested with a stand-in.
    private const string Code = "test-code";
    private static bool Matches(string code) => code == Code;
    [Fact] public void OnlyTheHashedCodeIsAccepted()
    {
        Assert.False(AdvancedMixAccess.IsAccessCode("wrong"));
        string? real = Environment.GetEnvironmentVariable("SUKUT_ADVANCED_CODE");
        if (real != null) Assert.True(AdvancedMixAccess.IsAccessCode(real));
    }
    [Fact] public void ThreeWrongEntriesLockForTenMinutesIncludingCorrectCodeDuringLock()
    {
        var access=new AdvancedMixAccess();
        Assert.Equal(AdvancedMixAccess.Result.Incorrect,access.Attempt("wrong",1000));
        Assert.Equal(AdvancedMixAccess.Result.Incorrect,access.Attempt("wrong",1000));
        Assert.Equal(AdvancedMixAccess.Result.Locked,access.Attempt("wrong",1000));
        Assert.Equal(601000,access.LockedUntil);
        Assert.Equal(AdvancedMixAccess.Result.Locked,access.Attempt(Code,600999,Matches));
        Assert.Equal(AdvancedMixAccess.Result.Enabled,access.Attempt(Code,601000,Matches));
    }
    [Fact] public async Task ClosingAndReopeningTheDialogKeepsTheAttemptCountAndLock()
    {
        string folder=Path.Combine(Path.GetTempPath(),Guid.NewGuid().ToString("N"));
        string path=Path.Combine(folder,"access");
        try {
            var access=new AdvancedMixAccess();access.Attempt("incorrect",1000);access.Attempt("incorrect",1000);await access.Save(path);
            access=AdvancedMixAccess.Load(path);Assert.Equal(2,access.Failures);
            Assert.Equal(AdvancedMixAccess.Result.Locked,access.Attempt("incorrect",1000));await access.Save(path);
            Assert.Equal(AdvancedMixAccess.Result.Locked,AdvancedMixAccess.Load(path).Attempt(Code,2000,Matches));
        } finally { if(Directory.Exists(folder))Directory.Delete(folder,true); }
    }
    [Theory] [InlineData(0)] [InlineData(1)] [InlineData(3)]
    public void AdvancedValuesCannotChangeAnotherMode(int mode)
    {
        Assert.Equal(AudioModePreset.ForMode(mode,5),AudioModePreset.ForMode(mode,5,true,10,10,10));
    }
    [Fact] public void StandardPresetsKeepTheirMuteWhileAdvancedMinimumIsFinite()
    {
        var normal=AudioModePreset.ForMode(2,-10);
        Assert.True(normal.MuteMusic);
        var advanced=AudioModePreset.ForMode(2,-10,true,0,-20,-10);
        Assert.Equal(-20,advanced.MusicDb);Assert.False(advanced.MuteMusic);
        Assert.Equal(normal,AudioModePreset.ForMode(2,-10,false,10,10,10));
    }
    [Fact] public void EveryAdvancedStemIsBoundedToMinusTwentyPlusTenWithoutMute()
    {
        var mix=AudioModePreset.ForMode(2,0,true,-100,0,100);
        Assert.Equal(-20,mix.VoiceDb);Assert.Equal(1,mix.MusicLinear);Assert.Equal(10,mix.BackgroundDb);
        var lowest=AudioModePreset.ForMode(2,0,true,0,-200,-200);
        Assert.Equal(-20,lowest.MusicDb);Assert.Equal(-20,lowest.BackgroundDb);
        Assert.False(lowest.MuteMusic);Assert.False(lowest.MuteBackground);
    }
}
