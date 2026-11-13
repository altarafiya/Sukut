using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;
using Windows.System;

namespace Sukut;

public sealed partial class ShellView
{
    private readonly DispatcherTimer _advancedHold = new() { Interval = TimeSpan.FromMilliseconds(AdvancedMixAccess.HoldMilliseconds) };
    private Point _advancedPress;
    private ContentDialog? _advancedDialog;
    private void InitializeAdvancedMixHold()
    {
        AdvancedAccessNotice.Closed += (_, _) => AdvancedAccessNotice.Visibility=Visibility.Collapsed;
        _advancedHold.Tick += async (_, _) =>
        {
            _advancedHold.Stop();
            if (_quitting || !_visible || Minimized || !OptimizedCard.IsEnabled || HomePanel.Visibility != Visibility.Visible) return;
            OptimizedCard.ReleasePointerCaptures();
            if(_preferences.AdvancedOptimized) await DisableAdvancedMix(); else await OpenAdvancedMix();
        };
        OptimizedCard.AddHandler(UIElement.PointerPressedEvent, new PointerEventHandler((_, e) =>
        {
            var point = e.GetCurrentPoint(OptimizedCard);
            if (!point.Properties.IsLeftButtonPressed && e.Pointer.PointerDeviceType == Microsoft.UI.Input.PointerDeviceType.Mouse) return;
            _advancedPress = point.Position; _advancedHold.Stop(); _advancedHold.Interval=TimeSpan.FromMilliseconds(AdvancedMixAccess.HoldDuration(_preferences.AdvancedOptimized)); _advancedHold.Start();
        }), true);
        OptimizedCard.AddHandler(UIElement.PointerMovedEvent, new PointerEventHandler((_, e) =>
        {
            var point = e.GetCurrentPoint(OptimizedCard).Position;
            if (Math.Abs(point.X - _advancedPress.X) > 10 || Math.Abs(point.Y - _advancedPress.Y) > 10) _advancedHold.Stop();
        }), true);
        foreach (var signal in new[] { UIElement.PointerReleasedEvent, UIElement.PointerCanceledEvent, UIElement.PointerCaptureLostEvent, UIElement.PointerExitedEvent })
            OptimizedCard.AddHandler(signal, new PointerEventHandler((_, _) => _advancedHold.Stop()), true);
        OptimizedCard.AddHandler(UIElement.KeyDownEvent, new KeyEventHandler((_, e) =>
        {
            if (e.Key is VirtualKey.Space or VirtualKey.Enter && !e.KeyStatus.WasKeyDown) { _advancedHold.Stop(); _advancedHold.Interval=TimeSpan.FromMilliseconds(AdvancedMixAccess.HoldDuration(_preferences.AdvancedOptimized)); _advancedHold.Start(); }
        }), true);
        OptimizedCard.AddHandler(UIElement.KeyUpEvent, new KeyEventHandler((_, _) => _advancedHold.Stop()), true);
        OptimizedCard.Unloaded += (_, _) => _advancedHold.Stop();
        HomePanel.RegisterPropertyChangedCallback(UIElement.VisibilityProperty, (_, _) => _advancedHold.Stop());
        _window.Activated += (_, e) => { if (e.WindowActivationState == WindowActivationState.Deactivated) _advancedHold.Stop(); };

    }
    private async Task DisableAdvancedMix()
    {
        _advancedDialog?.Hide();
        _preferences.AdvancedOptimized=false;_selectedMode=2;Save();RenderModeDetails(2);
        if(_mode!=0 && !_command)await ChangeMode(2,force:true);
    }
    private readonly List<ReleasedSlider> _advancedSliders = [];
    private readonly List<TextBlock> _advancedValues = [];
    private readonly List<TextBlock> _advancedNames = [];
    private readonly List<FontIcon> _advancedIcons = [];
    private void BuildAdvancedMixSliders()
    {
        for (int stem = 0; stem < 3; stem++)
        {
            int index = stem;
            string label = stem == 0 ? L("Voice", "الصوت البشري") : stem == 1 ? L("Music", "الموسيقى") : L("Background", "الخلفية");
            
            var brush = PhoneBrush(stem==0?"AccentText":stem==1?"GainMuted":"GainReduced");
            var row = new Grid { ColumnSpacing = 10 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var icon = new FontIcon { Glyph = stem == 0 ? "\uE720" : stem == 1 ? "\uE8D6" : "\uE767", FontSize = 18, Foreground = brush };
            row.Children.Add(icon); _advancedIcons.Add(icon);
            var name = new TextBlock { Text = label, Foreground = brush, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold, TextWrapping = TextWrapping.Wrap };
            Grid.SetColumn(name, 1); row.Children.Add(name); _advancedNames.Add(name);
            var value = new TextBlock { Foreground = brush, FlowDirection = FlowDirection.LeftToRight, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold };
            Grid.SetColumn(value, 2); row.Children.Add(value); _advancedValues.Add(value);
            AdvancedMixPanel.Children.Add(row);
            var slider = new ReleasedSlider { Minimum = -20, Maximum = 10, StepFrequency = 1, Foreground = brush };
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(slider, label);
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetAutomationId(slider, "AdvancedMix" + stem);
            slider.ValueChanged += (_, e) => { if (!_applying) value.Text = AdvancedGainText((int)Math.Round(e.NewValue)); };
            slider.ValueCommitted += async (_, _) =>
            {
                if (_applying || _quitting) return;
                int db = Math.Clamp((int)Math.Round(slider.Value), -20, 10);
                int previous = index == 0 ? _preferences.AdvancedVoiceDb : index == 1 ? _preferences.AdvancedMusicDb : _preferences.AdvancedBackgroundDb;
                if (db == previous) return;
                if (index == 0) _preferences.AdvancedVoiceDb = db; else if (index == 1) _preferences.AdvancedMusicDb = db; else _preferences.AdvancedBackgroundDb = db;
                Save(); _backgroundCommitPending = true; await ApplyCommittedBackground();
            };
            _advancedSliders.Add(slider); AdvancedMixPanel.Children.Add(slider);
        }
    }
    private string AdvancedGainText(int db) => (db > 0 ? "+" : "") + db + " dB";
    private void RenderAdvancedMix(int mode)
    {
        if(_preferences.AdvancedOptimized && _advancedSliders.Count==0) BuildAdvancedMixSliders();
        AdvancedMixPanel.Visibility = mode == 2 && _preferences.AdvancedOptimized ? Visibility.Visible : Visibility.Collapsed;
        int[] values = [_preferences.AdvancedVoiceDb, _preferences.AdvancedMusicDb, _preferences.AdvancedBackgroundDb];
        for (int i = 0; i < _advancedSliders.Count; i++)
        {
            string label=i==0?L("Voice","الصوت البشري"):i==1?L("Music","الموسيقى"):L("Background","الخلفية");
            _advancedNames[i].Text=label;
            var brush=PhoneBrush(i==0?"AccentText":i==1?"GainMuted":"GainReduced");
            _advancedNames[i].Foreground=brush; _advancedValues[i].Foreground=brush; _advancedIcons[i].Foreground=brush; _advancedSliders[i].Foreground=brush;
            Microsoft.UI.Xaml.Automation.AutomationProperties.SetName(_advancedSliders[i],label);
            if (!_advancedSliders[i].IsInteracting) _advancedSliders[i].Value = values[i];
            _advancedValues[i].Text = AdvancedGainText((int)Math.Round(_advancedSliders[i].Value));
        }
    }
    private void ShowAdvancedLock(long remaining) {
        AdvancedAccessNotice.Title=L("Advanced mix locked","المزج المتقدم مقفل");
        AdvancedAccessNotice.Message=L($"Try again in {(remaining+59999)/60000} minutes.",$"أعد المحاولة بعد {(remaining+59999)/60000} دقائق.");
        AdvancedAccessNotice.Visibility=Visibility.Visible;AdvancedAccessNotice.IsOpen=true;
    }
    private async Task OpenAdvancedMix()
    {
        if (_advancedDialog != null) return;
        string path = Path.Combine(_root, "runtime", "advanced-mix-access");
        var access = AdvancedMixAccess.Load(path);
        long remaining = access.LockedUntil - DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        if (remaining > 0) { ShowAdvancedLock(remaining); return; }
        var content = new StackPanel { Spacing = 12, MinWidth = 260 };
        content.Children.Add(new FontIcon { Glyph = "\uE9E9", FontSize = 30, HorizontalAlignment = HorizontalAlignment.Left, Foreground = PhoneBrush("AccentText") });
        content.Children.Add(new TextBlock { Text = L("Control Voice, Music and Background in 1 dB steps, from −20 to +10 dB. The lowest step reduces sound; it does not mute it.", "تحكّم في الصوت البشري والموسيقى والخلفية بخطوات قدرها 1 dB، من ‎−20 إلى ‎+10 dB. أقل قيمة تخفّض الصوت ولا تكتمه."), TextWrapping = TextWrapping.Wrap });
        var input = new PasswordBox { PlaceholderText = L("Enter the access code", "أدخل رمز الوصول"), FlowDirection = FlowDirection.LeftToRight };
        var error = new TextBlock { TextWrapping = TextWrapping.Wrap };
        if (!_preferences.AdvancedOptimized) { content.Children.Add(input); content.Children.Add(error); }
        bool disabling = _preferences.AdvancedOptimized;
        var dialog = new ContentDialog {
            XamlRoot = Root.XamlRoot, RequestedTheme = Root.ActualTheme, FlowDirection = Root.FlowDirection,
            Title = L("Advanced Optimized mix", "المزج المتقدم للوضع المحسّن"), Content = content,
            PrimaryButtonText = disabling ? L("Use standard Optimized", "استخدام الوضع المحسّن العادي") : L("Enable advanced mode", "تفعيل الوضع المتقدم"),
            CloseButtonText = L("Cancel", "إلغاء"), DefaultButton = ContentDialogButton.Primary
        };
        dialog.PrimaryButtonClick += async (_, e) =>
        {
            var deferral = e.GetDeferral(); e.Cancel = true;
            try
            {
                if (!disabling)
                {
                    var result = access.Attempt(input.Password, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
                    input.Password = ""; await access.Save(path);
                    if (result == AdvancedMixAccess.Result.Incorrect) { error.Text = L($"Incorrect code. Attempts remaining: {3-access.Failures}", $"رمز غير صحيح. المحاولات المتبقية: {3-access.Failures}"); return; }
                    if (result == AdvancedMixAccess.Result.Locked) { ShowAdvancedLock(AdvancedMixAccess.LockMilliseconds); e.Cancel = false; return; }
                    _preferences.AdvancedVoiceDb = 0; _preferences.AdvancedMusicDb = -20; _preferences.AdvancedBackgroundDb = _preferences.OptimizedBackgroundDb;
                }
                AdvancedAccessNotice.IsOpen=false; AdvancedAccessNotice.Visibility=Visibility.Collapsed;
                _preferences.AdvancedOptimized = !disabling; _selectedMode = 2; Save(); RenderModeDetails(2);
                if (_mode != 0 && !_command) await ChangeMode(2, force: true);
                e.Cancel = false;
            }
            catch (Exception failure) { error.Text = failure.Message; }
            finally { deferral.Complete(); }
        };
        _advancedDialog = dialog;
        try { await dialog.ShowAsync(); } finally { _advancedDialog = null; }
    }
}
