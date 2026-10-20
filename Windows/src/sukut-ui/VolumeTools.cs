using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Windows.System;

namespace Sukut;

public sealed partial class ShellView
{
    private string VolumeInstructions => L(
        "Open VB-CABLE as administrator. Under Options, leave Enable Windows Volume Control unchecked. It is normally off after a fresh installation. Use your normal Windows volume keys.",
        "افتح VB-CABLE كمسؤول. من Options اترك Enable Windows Volume Control دون علامة صح. يكون معطّلًا عادةً بعد التثبيت الجديد. استخدم أزرار صوت ويندوز المعتادة.");
    private void RenderCableVolumeTip()
    {
        if (CableVolumeTip == null) return;
        bool show = _preferences.SourcePid == 0 && !_preferences.VolumeGuideDismissed && !_preferences.CableVolumeDisabledConfirmed;
        CableVolumeTip.Title = L("Check volume control", "تأكّد من التحكم بالصوت");
        CableVolumeTip.Message = VolumeInstructions;
        CableVolumeTipButton.Content = L("Open VB-CABLE as administrator", "فتح VB-CABLE كمسؤول");
        CableVolumeTip.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        CableVolumeTip.IsOpen = show;
    }
    private void CableVolumeTip_Closed(InfoBar sender, InfoBarClosedEventArgs args)
    {
        if (args.Reason != InfoBarCloseReason.CloseButton) return;
        _preferences.VolumeGuideDismissed = true; Save(); sender.Visibility = Visibility.Collapsed;
    }

    private void RenderVolumeTools()
    {
        if (SettingsPanel == null) return;
        CablePanelLabel.Text = L("VB-CABLE · Administrator", "VB-CABLE · مسؤول");
        AutomationProperties.SetName(CablePanelButton, L("Open VB-CABLE control panel as administrator", "فتح لوحة تحكم VB-CABLE كمسؤول"));
        EffectsHelpHeader.Text = L("Headphone effects / Dolby", "مؤثرات السماعات / Dolby");
        AutomationProperties.SetName(EffectsHelp, EffectsHelpHeader.Text);
        EffectsHelpText.Text = L(
            "For Dolby, choose one Source app under Advanced so Windows keeps your headphones selected. Then enable Dolby Atmos for Headphones in Windows Sound → Spatial sound. Processing stays stereo; effects depend on the driver and Dolby Access.",
            "لاستخدام Dolby، اختر تطبيقًا واحدًا من «التطبيق المصدر» ضمن «متقدم» لإبقاء سماعاتك مختارة في ويندوز. ثم فعّل Dolby Atmos for Headphones من إعدادات الصوت ← الصوت المكاني. يبقى الصوت المعالج ستيريو، ويعتمد دعم المؤثرات على برنامج التشغيل وDolby Access.");
        SoundSettingsLabel.Text = L("Open Windows Sound", "فتح إعدادات صوت ويندوز");
        ChooseSourceAppLabel.Text = L("Choose a source app", "اختيار التطبيق المصدر");
        AutomationProperties.SetName(SoundSettingsButton, SoundSettingsLabel.Text);
        AutomationProperties.SetName(ChooseSourceAppButton, ChooseSourceAppLabel.Text);
    }

    private void OpenCablePanel_Click(object sender, RoutedEventArgs args)
    {
        // Only known local installation locations are searched; no executable
        // is downloaded, bundled, or resolved from a writable search PATH.
        string[] candidates =
        [
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "VB", "CABLE", "VBCABLE_ControlPanel.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "VB", "CABLE", "VBCABLE_ControlPanel.exe"),
            Path.Combine(Environment.SystemDirectory, "VBCABLE_ControlPanel.exe"),
            Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "SysWOW64", "VBCABLE_ControlPanel.exe")
        ];
        try
        {
            string? panel = Array.Find(candidates, File.Exists);
            if (panel == null)
            {
                Error(L("VB-CABLE control panel was not found. Open VBCABLE_ControlPanel.exe from your VB-CABLE installation folder, or reinstall VB-CABLE from Settings → Setup.",
                    "لم يتم العثور على لوحة VB-CABLE. افتح VBCABLE_ControlPanel.exe من مجلد تثبيت VB-CABLE، أو أعد تثبيته من الإعدادات ← الإعداد الأولي."));
                return;
            }
            Process.Start(new ProcessStartInfo(panel)
            {
                UseShellExecute = true, Verb = "runas", WorkingDirectory = Path.GetDirectoryName(panel)!
            });
        }
        // Cancelling UAC leaves the tip and its manual confirmation unchanged.
        catch (Win32Exception error) when (error.NativeErrorCode == 1223) { }
        catch (Exception error) { Error(L("Could not open VB-CABLE: ", "تعذّر فتح VB-CABLE: ") + error.Message); }
    }

    private async void OpenSoundSettings_Click(object sender, RoutedEventArgs args)
    {
        try { await Launcher.LaunchUriAsync(new Uri("ms-settings:sound")); }
        catch (Exception error) { Error(error.Message); }
    }
    private void ChooseSourceApp_Click(object sender, RoutedEventArgs args)
    {
        _showSourceApp = true;
        if (_page == 1 && SettingsPanel != null) FocusRequestedSourceApp();
        else Navigate(1);
    }
    private bool _showSourceApp;
    private void FocusRequestedSourceApp()
    {
        if (!_showSourceApp || AdvancedSection == null) return;
        _showSourceApp = false;
        DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
        {
            if (_quitting || AppChoice?.IsLoaded != true) return;
            AppChoice.StartBringIntoView(); AppChoice.Focus(FocusState.Programmatic);
        });
    }
}
