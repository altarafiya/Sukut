using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Sukut;
public sealed partial class ShellView
{
    private TextBlock? _phoneNameText,_phoneConnectionText,_phoneVersionText;
    private FontIcon? _phoneConnectionIcon;
    private Button? _phoneRefreshButton;
    private PhoneConnectionInfo? _phoneInfo;
    private string? _phoneInfoSerial;
    private bool _applyingPhone;
    private Task<string>? _phoneRefreshTask;
    private Border BuildPhoneConnectionCard()
    {
        var content=new StackPanel {Spacing=6};
        var row=new Grid {ColumnSpacing=14};row.ColumnDefinitions.Add(new(){Width=GridLength.Auto});row.ColumnDefinitions.Add(new(){Width=new GridLength(1,GridUnitType.Star)});row.ColumnDefinitions.Add(new(){Width=GridLength.Auto});
        _phoneConnectionIcon=new FontIcon {Glyph="\uE8EA",FontSize=PhoneIconSize,Width=PhoneIconSize,Height=PhoneIconSize,VerticalAlignment=VerticalAlignment.Center};row.Children.Add(_phoneConnectionIcon);
        var words=new StackPanel {Spacing=3};Grid.SetColumn(words,1);
        _phoneNameText=new TextBlock {FontSize=14,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap};
        _phoneConnectionText=new TextBlock {Style=PhoneText("CaptionTextBlockStyle"),TextWrapping=TextWrapping.Wrap};
        _phoneVersionText=new TextBlock {Style=PhoneText("CaptionTextBlockStyle"),TextWrapping=TextWrapping.Wrap};
        words.Children.Add(_phoneNameText);words.Children.Add(_phoneConnectionText);words.Children.Add(_phoneVersionText);row.Children.Add(words);
        _phoneRefreshButton=new Button {Content=new FontIcon {Glyph="\uE72C",FontSize=PhoneSmallIconSize},Width=32,Height=32,MinWidth=32,MinHeight=32,Padding=new Thickness(4),CornerRadius=new CornerRadius(4),VerticalAlignment=VerticalAlignment.Center};
        AutomationProperties.SetName(_phoneRefreshButton,L("Refresh connected phone","تحديث الهاتف المتصل"));ToolTipService.SetToolTip(_phoneRefreshButton,L("Refresh","تحديث"));
        _phoneRefreshButton.Click+=async(_,_)=>await PhoneAction(RefreshPhoneDevices);Grid.SetColumn(_phoneRefreshButton,2);row.Children.Add(_phoneRefreshButton);
        content.Children.Add(row);content.Children.Add(PhoneDevicePicker());UpdatePhoneConnectionCard();
        return new Border {Padding=new Thickness(8,10,8,10),Child=content};
    }
    private void UpdatePhoneConnectionCard()
    {
        if(_phoneNameText==null)return;
        var device=_phoneSelected;bool connected=device?.State=="device";
        var info=_phoneInfoSerial==device?.Serial?_phoneInfo:null;
        _phoneNameText.Text=info?.Name??device?.Model??L("No phone connected","لا يوجد هاتف متصل");
        _phoneConnectionText!.Text=connected?L("Connected via USB","متصل عبر USB")+(string.IsNullOrEmpty(info?.Android)?"":" · Android "+info.Android)
            :device?.State=="unauthorized"?L("Approve USB debugging on your phone","وافق على تصحيح USB في هاتفك")
            :_phoneKnown.Count>1?L("Choose your phone below","اختر هاتفك أدناه"):L("Connect your phone by USB","وصّل هاتفك عبر USB");
        _phoneConnectionIcon!.Foreground=PhoneBrush(connected?"GainRaised":"ModeOffText");
        _phoneVersionText!.Visibility=connected&&info!=null?Visibility.Visible:Visibility.Collapsed;
        _phoneVersionText.Text=info?.SukutCode==null?L("Sukut not installed","سُكوت غير مثبّت")
            :"Sukut "+(info.SukutVersion??info.SukutCode.ToString())+" · "+(info.WorkerReady?L("Worker ready","عامل الصوت جاهز"):L("Activation needed","يلزم التفعيل"));
        _phoneVersionText.Foreground=PhoneBrush(info?.WorkerReady==true?"GainRaised":"ModeOptimizedText");
        if(_phoneDevices!=null)_phoneDevices.Visibility=_phoneKnown.Count>1?Visibility.Visible:Visibility.Collapsed;
    }
    private async Task ReadPhoneInfo(PhoneDevice device)
    {
        if(device.State!="device"){_phoneInfo=null;UpdatePhoneConnectionCard();return;}
        try {
            var info=await _phoneTools.InspectPhone(device);
            if(_phoneSelected?.Serial==device.Serial){_phoneInfo=info;_phoneInfoSerial=device.Serial;}
        } catch {if(_phoneSelected?.Serial==device.Serial)_phoneInfo=null;}
        UpdatePhoneConnectionCard();
    }
    private async Task RefreshPhoneQuietly()
    {
        if(!_phoneTools.AdbReady||_phoneBusy)return;
        try {await RefreshPhoneDevices();}catch{UpdatePhoneConnectionCard();}
    }
    private Task<string> RefreshPhoneDevices()
    {
        if(_phoneRefreshTask is {IsCompleted:false})return _phoneRefreshTask;
        return _phoneRefreshTask=RefreshPhoneDevicesCore();
    }
}
