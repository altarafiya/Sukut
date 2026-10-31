using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Sukut;
public sealed partial class ShellView
{
    private int _selectedMode=1;
    private WindowModeIcons? _windowModeIcons;
    private static uint ModeRgb(int mode,bool dark)=>mode switch {
        1=>dark?0xAAA5FFu:0x5B52C7u,2=>dark?0xE7C48Eu:0x795315u,
        3=>dark?0xF2A7ABu:0xA02C3Bu,_=>dark?0xACBBD5u:0x4F607Du};
    private string HomeModeName(int mode)=>mode switch {2=>L("Optimized","محسّن"),3=>L("Strict","صارم"),_=>L("Music Filter","مرشّح الموسيقى")};
    // Compact desktop Start/Stop command: status remains separate from the action.
    // On/Starting = solid accent + contrasting symbol; Standby = solid accent + sleeping face.
    private enum PowerLook { Off, On, Standby, Attention }
    private void RefreshHomeAppearance(bool refreshIcons=false)
    {
        _windowModeIcons?.SetMode(_mode,refreshIcons);
        if(PowerButton==null)return;
        if(refreshIcons)PowerButton.Tag=null; // Theme changed: re-resolve the brushes.
        var look=_status=="Attention"?PowerLook.Attention:_status=="Active" && _mode!=0 && _state?.Standby==1?PowerLook.Standby:_mode!=0 || _command?PowerLook.On:PowerLook.Off;
        // Standby (5 s of silence): explain it like the phone, next to the sleeping face.
        HeroModeLabel.Text=look==PowerLook.Standby
            ?L("Nothing is playing. Sukut rests to save power and wakes as soon as sound starts.","لا يوجد صوت قيد التشغيل. يرتاح سُكوت لتوفير الطاقة ويعود فور بدء الصوت.")
            :HomeModeName(_selectedMode);
        if(PowerButton.Tag is not PowerLook shown || shown!=look) {
        // Resolve from Sukut's theme dictionary for this window's actual theme (the app
        // can be Light/Dark independently of Windows), not the application default.
        string theme=new Windows.UI.ViewManagement.AccessibilitySettings().HighContrast?"HighContrast":Root.ActualTheme==ElementTheme.Light?"Light":"Default";
        var palette=(ResourceDictionary)Application.Current.Resources.ThemeDictionaries[theme];
        Brush Resource(string key)=>(Brush)palette[key];
        switch(look)
        {
            case PowerLook.Off: PowerButton.Background=Resource("SukutAccentContainerBrush"); PowerButton.BorderBrush=Resource("SukutAccentBrush"); PowerButton.BorderThickness=new Thickness(1.5); PowerGlyph.Foreground=Resource("SukutAccentBrush"); break;
            case PowerLook.Attention: PowerButton.Background=Resource("SukutWarningContainerBrush"); PowerButton.BorderBrush=Resource("SukutWarningBrush"); PowerButton.BorderThickness=new Thickness(1.5); PowerGlyph.Foreground=Resource("SukutWarningBrush"); break;
            default: PowerButton.Background=Resource("SukutAccentBrush"); PowerButton.BorderThickness=new Thickness(0); PowerGlyph.Foreground=Resource("SukutOnAccentBrush"); break;
        }
        PowerGlyph.Visibility=Visibility.Visible;
        StatusStateGlyph.Visibility=look==PowerLook.Standby?Visibility.Collapsed:Visibility.Visible;
        StatusStateGlyph.Foreground=Resource(look==PowerLook.Attention?"SukutWarningBrush":look==PowerLook.On?"GainRaisedBrush":"ModeOffBrush");
        SleepGlyph.Visibility=look==PowerLook.Standby?Visibility.Visible:Visibility.Collapsed;
        PowerButton.Tag=look;
        }
        string stateGlyph=look==PowerLook.Attention?"\uE7BA":_command || _status is "Preparing" or "Reconnecting" or "Recovering"?"\uE823":look==PowerLook.On?"\uE768":"\uE7E8";
        if(StatusStateGlyph.Glyph!=stateGlyph)StatusStateGlyph.Glyph=stateGlyph;
        PowerActionLabel.Text=_command?L("Working…","جارٍ التنفيذ…"):_mode==0?L("Start","تشغيل"):L("Stop","إيقاف");
        PowerActionLabel.Foreground=PowerGlyph.Foreground;
        AutomationProperties.SetName(PowerButton,_mode==0?L("Turn on ","تشغيل ")+HomeModeName(_selectedMode):L("Turn off Sukut","إيقاف سُكوت"));
        string tip=_mode==0?L("Turn on","تشغيل"):L("Turn off","إيقاف");
        if(!Equals(ToolTipService.GetToolTip(PowerButton),tip))ToolTipService.SetToolTip(PowerButton,tip);
    }
    private async void Power_Click(object sender,RoutedEventArgs args)
    {
        if(_command || !_initialized || (_state?.HostCommand??0)!=0)return;
        if(_mode==0 && !ModeSetupReady){ShowSetup();return;}
        await ChangeMode(_mode==0?_selectedMode:0);
    }
    // Wide windows: modes beside the Playback box. Narrow: the box moves below.
    private void HomeLayout_SizeChanged(object sender,SizeChangedEventArgs args)
    {
        bool wide=args.NewSize.Width>=700;
        double side=wide?Math.Clamp(Math.Round(args.NewSize.Width*.46),320,400):0;
        if(HomeSideColumn.Width.Value!=side)HomeSideColumn.Width=new GridLength(side);
        HomeLayout.ColumnSpacing=wide?20:0;
        Grid.SetRow(PlaybackBox,wide?1:2);Grid.SetColumn(PlaybackBox,wide?1:0);
    }
    private void RefreshApps_Click(object sender,RoutedEventArgs args)=>_=RefreshRoutingChoices(includeApps:true);
    private void DesktopNavigation_SelectionChanged(NavigationView sender,NavigationViewSelectionChangedEventArgs args)
    {
        if(_applying || _setupVisible || _quitting)return;
        if(args.SelectedItemContainer is NavigationViewItem item && int.TryParse(item.Tag?.ToString(),out int page) && page!=_page)Navigate(page);
    }
}
