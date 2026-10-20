using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace Sukut;

public sealed class ShellState : INotifyPropertyChanged
{
    private bool _home, _settings, _about, _setup, _phone;
    public bool PhoneLoaded { get => _phone; set => Set(ref _phone, value); }
    public bool HomeLoaded { get => _home; set => Set(ref _home, value); }
    public bool SettingsLoaded { get => _settings; set => Set(ref _settings, value); }
    public bool AboutLoaded { get => _about; set => Set(ref _about, value); }
    public bool SetupLoaded { get => _setup; set => Set(ref _setup, value); }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Set(ref bool field, bool value, [CallerMemberName] string? name = null)
    {
        if (field == value) return;
        field = value;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
    }
}
