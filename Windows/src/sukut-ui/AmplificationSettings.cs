using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Sukut;

public sealed partial class ShellView
{
    private void RenderAmplification()
    {
        if (AmplificationSlider == null) return;
        _preferences.AmplificationDb = Math.Clamp(_preferences.AmplificationDb, 0, 12);
        AmplificationHeading.Text = L("Audio amplification", "تضخيم الصوت");
        if (!AmplificationSlider.IsInteracting) AmplificationSlider.Value = _preferences.AmplificationDb;
        RenderAmplificationValue();
        AmplificationNote.Text = L("Processing modes only. Peak protection limits boost on loud audio.", "يعمل مع أوضاع المعالجة فقط. تحدّ الحماية من القمم من التضخيم عندما يكون الصوت مرتفعًا.");
        AutomationProperties.SetName(AmplificationSlider, AmplificationHeading.Text);
    }
    private void RenderAmplificationValue(int? preview = null)
    {
        int value = preview ?? _preferences.AmplificationDb;
        if (AmplificationValue != null) AmplificationValue.Text = (value > 0 ? "+" : "") + value.ToString(System.Globalization.CultureInfo.InvariantCulture) + " dB";
    }
    private void Amplification_Changed(object sender, RangeBaseValueChangedEventArgs args)
    {
        if (!_applying && !_quitting) RenderAmplificationValue(Math.Clamp((int)Math.Round(args.NewValue), 0, 12));
    }
    private async void Amplification_Committed(object? sender, EventArgs args)
    {
        if (_applying || _quitting || !_setupEngineReady || sender is not ReleasedSlider slider) return;
        int previous = _preferences.AmplificationDb, value = Math.Clamp((int)Math.Round(slider.Value), 0, 12);
        if (value == previous) return;
        _preferences.AmplificationDb = value; RenderAmplificationValue();
        try { await _engine.SetAmplification(value); Save(); }
        catch (Exception error)
        {
            _preferences.AmplificationDb = previous;
            if (SettingsPanel != null) RenderSettings();
            Error(error.Message);
        }
    }
}
