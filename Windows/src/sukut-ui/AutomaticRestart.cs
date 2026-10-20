using Microsoft.UI.Xaml;

namespace Sukut;

public sealed partial class ShellView
{
    private readonly AudioRestartPolicy _audioRestart = new();
    private readonly DispatcherTimer _restartTimer = new();
    private bool _automaticRestarting;
    private string _automaticFailure = "";

    private void CancelAutomaticRestart(bool reset = true)
    {
        _restartTimer.Stop();
        if (reset) _audioRestart.Reset();
    }

    private static bool NeedsUserAction(string error) => error.Contains("Windows Volume Control", StringComparison.OrdinalIgnoreCase)
        || error.Contains("confirm", StringComparison.OrdinalIgnoreCase)
        || error.Contains("restoration", StringComparison.OrdinalIgnoreCase)
        || error.Contains("source app is no longer running", StringComparison.OrdinalIgnoreCase)
        || error.Contains("reopen the source app", StringComparison.OrdinalIgnoreCase)
        || error.Contains("close the CleanWave preview", StringComparison.OrdinalIgnoreCase);

    private void AudioFault(string message, bool overload = false)
    {
        if (_automaticRestarting) { Status("Reconnecting"); return; }
        if (_mode == 0 || !ModeSetupReady || !_initialized || NeedsUserAction(message))
        {
            CancelAutomaticRestart();
            Error(message); return;
        }
        if (_audioRestart.Request(Environment.TickCount64))
        {
            _rawError = ""; _errorOpen = false; RenderError();
            _overloadTipOpen = false; RenderOverloadTip();
            Status("Reconnecting");
            _restartTimer.Interval = TimeSpan.FromMilliseconds(Math.Max(1, _audioRestart.DueAt!.Value - Environment.TickCount64));
            _restartTimer.Start();
        }
        else if (overload)
        {
            _overloadTipOpen = true; RenderOverloadTip(); Status("Recovering");
        }
        else Error(message + L(" Automatic restart failed after 3 attempts.", " لم تنجح إعادة التشغيل التلقائية بعد 3 محاولات."));
    }

    private async Task RunAutomaticRestart()
    {
        _restartTimer.Stop();
        if (_quitting || _mode == 0 || !ModeSetupReady) { CancelAutomaticRestart(); return; }
        if (_command || _polling || (_state?.Busy ?? 0) != 0)
        {
            _restartTimer.Interval = TimeSpan.FromMilliseconds(200); _restartTimer.Start(); return;
        }
        if (!_audioRestart.TryBegin(Environment.TickCount64)) return;
        _automaticRestarting = true; _automaticFailure = "";
        bool restarted;
        try { restarted = await ChangeMode(_mode, force: true, restart: true, automatic: true); }
        finally { _automaticRestarting = false; }
        if (!_quitting)
        {
            await Poll(force: true);
            if (!restarted && !string.IsNullOrWhiteSpace(_automaticFailure)) AudioFault(_automaticFailure);
        }
    }
}
