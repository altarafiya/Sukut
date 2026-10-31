using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace Sukut;

public sealed partial class ShellView
{
    private long _lastEndpointVersion, _lastRoutingRefresh;
    private Task<EngineState>? _dropdownRefresh;
    private Task? _routingRefresh;
    private bool _routingAppsRequested;

    private void Routing_DropDownOpened(object sender, object args)
    {
        // Keep the cached list open immediately. Enumeration and icon loading
        // must never gate a click or close/reopen the user's popup.
        QueueTypography();
        _ = RefreshRoutingChoices(ReferenceEquals(sender, AppChoice));
    }
    private Task RefreshRoutingChoices(bool includeApps)
    {
        if (!_setupEngineReady || _command || _quitting) return Task.CompletedTask;
        _routingAppsRequested |= includeApps;
        if (_routingRefresh is { IsCompleted: false }) return _routingRefresh;
        if (Environment.TickCount64 - _lastRoutingRefresh < 1500) return includeApps ? LoadApps() : Task.CompletedTask;
        return _routingRefresh = RefreshRoutingChoicesCore(includeApps);
    }
    private async Task RefreshRoutingChoicesCore(bool includeApps)
    {
        _lastRoutingRefresh = Environment.TickCount64;
        try
        {
            _dropdownRefresh = _engine.RefreshDevices();
            var state = await _dropdownRefresh;
            if (_quitting || _command) return;
            _state = state; _initialized = state.Initialized != 0; _setupScanned = state.Busy == 0;
            UpdateEndpoints(state);
            RenderEngineRow();
            if (SetupPanel != null) UpdateSetupState();
            // An app request arriving during device enumeration must not be lost.
            while (_routingAppsRequested && !_quitting)
            {
                _routingAppsRequested = false;
                await LoadApps();
            }
        }
        catch (Exception error) { if (!_quitting) Error(error.Message); }
    }
}
