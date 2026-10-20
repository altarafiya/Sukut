namespace Sukut;

public sealed class UiPreferences
{
    public string Language { get; set; } = "en";
    public string Theme { get; set; } = "system";
    public string Output { get; set; } = "";
    public string Source { get; set; } = "";
    public string Capture { get; set; } = "";
    public uint SourcePid { get; set; }
    public int Buffering { get; set; }
    public int AmplificationDb { get; set; }
    public int OptimizedBackgroundDb { get; set; }
    public bool SetupCompleted { get; set; }
    public bool CloseToTray { get; set; } = true;
    public bool CableVolumeDisabledConfirmed { get; set; }
    public bool VolumeGuideDismissed { get; set; }

    public UiPreferences Copy() => new()
    {
        Language = Language, Theme = Theme, Output = Output, Source = Source,
        Capture = Capture, SourcePid = SourcePid, Buffering = Math.Clamp(Buffering, 0, 2), AmplificationDb = Math.Clamp(AmplificationDb, 0, 12), SetupCompleted = SetupCompleted, CloseToTray = CloseToTray,
        CableVolumeDisabledConfirmed = CableVolumeDisabledConfirmed,
        VolumeGuideDismissed = VolumeGuideDismissed,
        OptimizedBackgroundDb = Math.Clamp(OptimizedBackgroundDb, -20, 10)
    };
}
