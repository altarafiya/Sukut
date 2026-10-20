using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace Sukut;

public sealed partial class ShellView
{
    private bool _backgroundCommitPending;
    private void OptimizedBackground_Changed(object sender, RangeBaseValueChangedEventArgs args)
    {
        if (_applying || _quitting) return;
        int value = Math.Clamp((int)Math.Round(args.NewValue), -20, 10);
        if (OptimizedBackgroundValue != null) OptimizedBackgroundValue.Text = (value > 0 ? "+" : "") + value.ToString(System.Globalization.CultureInfo.InvariantCulture) + " dB";
    }

    private async void OptimizedBackground_Committed(object? sender, EventArgs args)
    {
        if (_applying || _quitting || sender is not ReleasedSlider slider) return;
        int value = Math.Clamp((int)Math.Round(slider.Value), -20, 10);
        if (_preferences.OptimizedBackgroundDb == value) return;
        _preferences.OptimizedBackgroundDb = value;
        RenderModeDetails(_mode); Save();
        _backgroundCommitPending = true;
        await ApplyCommittedBackground();
    }

    private async Task ApplyCommittedBackground()
    {
        if (_command) return;
        _backgroundCommitPending = false;
        if (_quitting || _mode != 2 || !_initialized) return;
        // Commands are serialized by Engine; never restart the model for a gain.
        await ChangeMode(2, force: true);
    }

    private async void RestartAudio_Click(object sender, RoutedEventArgs args)
    {
        CancelAutomaticRestart();
        if (_mode != 0 && !_command) await ChangeMode(_mode, force: true, restart: true);
    }
}
