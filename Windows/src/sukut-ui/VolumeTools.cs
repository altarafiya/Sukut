using System.ComponentModel;
using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Windows.System;

namespace Sukut;

public sealed partial class ShellView
{
    private void RenderVolumeTools()
    {
        if (SettingsPanel == null) return;
        CablePanelLabel.Text = "VB-CABLE";
        AutomationProperties.SetName(CablePanelButton, L("Open VB-CABLE control panel as administrator", "فتح لوحة تحكم VB-CABLE كمسؤول"));
        EffectsHelpHeader.Text = L("Headphone effects / Dolby", "مؤثرات السماعات / Dolby");
        AutomationProperties.SetName(EffectsHelp, EffectsHelpHeader.Text);
        RenderHeadphoneEffects(EffectsHelpText);
        SoundSettingsLabel.Text = L("Windows Sound", "صوت ويندوز");
        AutomationProperties.SetName(SoundSettingsButton, SoundSettingsLabel.Text);
    }

    private void RenderHeadphoneEffects(TextBlock text)
    {
        text.Inlines.Clear();
        text.Inlines.Add(new Run {Text=L("For headphone effects, choose one ","لمؤثرات السماعات، اختر ")});
        text.Inlines.Add(new Run {Text=L("Source app","تطبيقًا مصدرًا واحدًا"),Foreground=PhoneBrush("ModeHalalText")});
        text.Inlines.Add(new Run {Text=L(" in Audio output. Then enable "," في مخرج الصوت. ثم فعّل ")});
        text.Inlines.Add(new Run {Text=L("Spatial sound","الصوت المكاني"),Foreground=PhoneBrush("ModeOptimizedText")});
        text.Inlines.Add(new Run {Text=L(" in Windows Sound."," في إعدادات صوت ويندوز.")});
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
