using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private long _recoveryUntil, _recoverySession = -1, _pausedSince;
    private long _lastFallbacks, _lastRecoveries, _lastBypasses, _lastCovered;
    private readonly Queue<long> _overloadEvents = new();
    private bool _overloadTipOpen, _overloadTipDismissed, _showBuffering, _overloadDetected;

    private void RenderBufferingSettings()
    {
        _preferences.Buffering = Math.Clamp(_preferences.Buffering, 0, 2);
        HomeBuffering?.Render(Arabic, _preferences.Buffering);
        SetupBuffering?.Render(Arabic, _preferences.Buffering);
        RenderEngineRow();
        if (_showBuffering)
        {
            _showBuffering = false;
            DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
            {
                if (_quitting || !_visible || BufferingButton?.IsLoaded != true) return;
                BufferingButton.Flyout?.ShowAt(BufferingButton);
            });
        }
    }

    private async void Buffering_Committed(object? sender, EventArgs args)
    {
        if (_applying || _command || sender is not BufferingControl control) return;
        int previous = _preferences.Buffering, level = control.Level;
        if (previous == level) return;
        _preferences.Buffering = level;
        if (_mode != 0 && !await ChangeMode(_mode, true))
        {
            string failure = _rawError;
            _preferences.Buffering = previous;
            // Restore the previous route/profile after a failed reconfiguration.
            await ChangeMode(_mode, true);
            Error(failure);
        }
        else
        {
            Save();
            _overloadEvents.Clear(); _overloadTipOpen = false; _overloadTipDismissed = false;
        }
        RenderBufferingSettings();
        RenderOverloadTip();
    }

    private void ObserveRecovery(EngineState state)
    {
        if (state.State != 1 || state.Session != _recoverySession)
        {
            _recoverySession = state.Session;
            _lastFallbacks = _lastRecoveries = _lastBypasses = _lastCovered = 0;
            _recoveryUntil = 0; _overloadEvents.Clear();
            _pausedSince = 0;
            _overloadTipOpen = _overloadTipDismissed = _overloadDetected = false;
        }
        if (state.State == 1)
        {
            long now = Environment.TickCount64;
            if (state.Paused == 0) _pausedSince = 0;
            else if (_pausedSince == 0) _pausedSince = now;
            long fallbacks = Math.Max(0, state.Fallbacks - _lastFallbacks);
            long covered = Math.Max(0, state.CoveredOverruns - _lastCovered);
            // Counters survive a short recovery between GUI snapshots. Keep
            // its indication visible for two seconds without increasing polling.
            if (state.Paused != 0 || fallbacks > 0 || state.Recoveries > _lastRecoveries || state.Bypasses > _lastBypasses)
                _recoveryUntil = now + 2000;
            while (_overloadEvents.TryPeek(out long time) && now - time > 30000) _overloadEvents.Dequeue();
            // Three actual fallbacks in 30 seconds, or 5 seconds paused,
            // indicate pressure. Covered spikes alone do not trigger the tip.
            // Successfully buffered spikes are diagnostics, not audible overload.
            int events = (int)Math.Min(6, Math.Min(fallbacks, 3) * 2);
            for (int i = 0; i < events && _overloadEvents.Count < 12; ++i) _overloadEvents.Enqueue(now);
            if ((_overloadEvents.Count >= 6 || _pausedSince != 0 && now - _pausedSince >= 5000) && !_overloadTipDismissed) _overloadDetected = true;
            _lastFallbacks = state.Fallbacks; _lastRecoveries = state.Recoveries;
            _lastBypasses = state.Bypasses; _lastCovered = state.CoveredOverruns;
        }
        RenderOverloadTip();
    }

    private void RenderOverloadTip()
    {
        if (OverloadTip == null) return;
        OverloadTip.Title = L("Audio needs more headroom", "الصوت يحتاج إلى هامش أكبر");
        OverloadTip.Message = _preferences.Buffering < 2
            ? L("Audio still needs more headroom after automatic restarts. Try more Audio buffering for steadier playback; it adds some delay.", "ما زال الصوت يحتاج إلى هامش أكبر بعد محاولات إعادة التشغيل. جرّب زيادة التخزين المؤقت لتشغيل أثبت، مع زيادة بسيطة في التأخير.")
            : L("Processing is still struggling after automatic restarts. Restart audio or reduce other demanding tasks.", "ما زالت المعالجة تواجه ضغطًا بعد محاولات إعادة التشغيل. أعد تشغيل الصوت أو خفّف المهام الأخرى الثقيلة.");
        BufferTipButton.Content = L("Buffering settings", "إعدادات التخزين المؤقت");
        RestartOverloadButton.Content = L("Restart audio", "إعادة تشغيل الصوت");
        BufferTipButton.Visibility = _preferences.Buffering < 2 ? Visibility.Visible : Visibility.Collapsed;
        bool open = _overloadTipOpen;
        OverloadTip.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
        OverloadTip.IsOpen = open;
    }

    private void OverloadTip_Closed(InfoBar sender, InfoBarClosedEventArgs args)
    {
        // Only a user's close suppresses subsequent tips for this session.
        if (args.Reason == InfoBarCloseReason.CloseButton) _overloadTipDismissed = true;
        _overloadTipOpen = false;
        sender.Visibility = Visibility.Collapsed;
    }

    private void OpenBufferingSettings_Click(object sender, RoutedEventArgs args)
    {
        _showBuffering = true;
        Navigate(0);
        RenderBufferingSettings();
    }
}
