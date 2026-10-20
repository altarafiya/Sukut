using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private long _lastEndpointVersion;
    private int _dropdownRequest;
    private ComboBox? _freshDropdown;
    private Task<EngineState>? _dropdownRefresh;
    private bool _outputFlyoutOpen;

    private void OutputFlyout_Opened(object sender, object args)
    {
        _outputFlyoutOpen = true;
        if (OutputSourceAppChoice != null) OutputSourceAppChoice.IsDropDownOpen = false;
        ArabicTypography.Apply(OutputFlyout.Content, Arabic); QueueTypography();
    }

    private void OutputFlyout_Closed(object sender, object args)
    {
        _outputFlyoutOpen = false;
        ++_dropdownRequest;
        _freshDropdown = null;
        if (OutputSourceAppChoice != null) OutputSourceAppChoice.IsDropDownOpen = false;
    }

    private async void Routing_DropDownOpened(object sender, object args)
    {
        if (sender is not ComboBox combo) return;
        if (_freshDropdown == combo) { _freshDropdown = null; QueueTypography(); return; }
        // Close synchronously in the opening callback, before the next rendered
        // frame. Only reopen after the native enumeration revision advances.
        combo.IsDropDownOpen = false;
        if (!_setupEngineReady || _command || _quitting) return;
        int request = ++_dropdownRequest;
        try
        {
            if (_dropdownRefresh == null || _dropdownRefresh.IsCompleted) _dropdownRefresh = _engine.RefreshDevices();
            var state = await _dropdownRefresh;
            if (_quitting) return;
            _state = state; _initialized = state.Initialized != 0; _setupScanned = state.Busy == 0;
            UpdateEndpoints(state); SetEnabled(true);
            if (SetupPanel != null) UpdateSetupState();
            if (combo == AppChoice || combo == OutputSourceAppChoice) await LoadApps();
            if (request != _dropdownRequest || !_visible || !combo.IsLoaded || combo.XamlRoot != Root.XamlRoot || combo == OutputSourceAppChoice && !_outputFlyoutOpen) return;
            // Reopen after the old popup's close/layout work has unwound.
            // This also lets the full-width advanced selectors finish measuring
            // their refreshed items before WinUI positions the popup.
            DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () =>
            {
                if (request != _dropdownRequest || _quitting || !_visible || !combo.IsLoaded || !combo.IsEnabled || combo.XamlRoot != Root.XamlRoot || combo == OutputSourceAppChoice && !_outputFlyoutOpen) return;
                _freshDropdown = combo;
                combo.IsDropDownOpen = true;
            });
        }
        catch (Exception error) { _freshDropdown = null; Error(error.Message); }
    }
}
