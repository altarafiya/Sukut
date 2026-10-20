#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>
#include "desktop/DesktopController.h"

using namespace cleanwave;
namespace {
constexpr UINT UpdateMessage=WM_APP+1, ClosedMessage=WM_APP+2;
enum Id {Off=101,Default=102,Custom=103,StartStop=110,Settings=111,
         Voice=121,Music=122,Background=123,Strength=124,Boost=125,
         SourceChoice=201,CaptureChoice=202,OutputChoice=203,Refresh=210,Apply=211,Cancel=212};
struct Palette {COLORREF bg,text,muted,panel,border,accent;};
class App {
 public:
  App(HINSTANCE instance,std::filesystem::path root,std::filesystem::path settings)
      :instance_(instance),root_(std::move(root)),settings_path_(std::move(settings)){}
  int Run(int show);
 private:
  static LRESULT CALLBACK MainProc(HWND h,UINT m,WPARAM w,LPARAM l);
  static LRESULT CALLBACK SettingsProc(HWND h,UINT m,WPARAM w,LPARAM l);
  LRESULT Handle(HWND h,UINT m,WPARAM w,LPARAM l,bool settings);
  HWND Child(HWND parent,const wchar_t* kind,const wchar_t* text,DWORD style,int id=0);
  void BuildHome();void OpenSettings();void Layout(bool resize=false);void Theme();void Fonts(bool settings=false);
  void Sync();void SyncSettings(bool populate=false);void SetMode(ProcessingMode mode);void Mix();
  void Paint(HWND window);void DrawButton(const DRAWITEMSTRUCT& item);void Close();
  void Place(HWND h,int x,int y,int width,int height,bool settings=false);
  void Text(HWND h,const std::wstring& text);
  void FillCombo(HWND combo,const std::vector<EndpointInfo>& list,const std::wstring& selected);
  std::wstring ComboId(HWND combo,const std::vector<EndpointInfo>& list);
  std::wstring EndpointName(const std::wstring& id) const;
  int Scale(int n) const {return MulDiv(n,static_cast<int>(dpi_),96);}
  HINSTANCE instance_;HWND window_=nullptr,settings_window_=nullptr;
  std::filesystem::path root_,settings_path_;
  std::unique_ptr<DesktopController> controller_;
  DesktopSnapshot snapshot_;DesktopPreferences preferences_;
  std::uint64_t submitted_=0,settings_submitted_=0,populated_endpoints_=0;
  std::wstring settings_error_;
  bool seeded_=false,closing_=false,dark_=false;
  UINT dpi_=96,settings_dpi_=96;int scroll_=0,content_height_=548;
  Palette colors_{};HBRUSH background_=nullptr,panel_=nullptr;
  HFONT body_=nullptr,title_=nullptr,heading_=nullptr;
  HFONT settings_body_=nullptr,settings_heading_=nullptr;
  HICON icon_=nullptr;
  HWND title_label_=nullptr,subtitle_=nullptr,status_=nullptr,route_=nullptr,mode_label_=nullptr,mode_hint_=nullptr;
  HWND custom_label_=nullptr,start_stop_=nullptr,settings_button_=nullptr,guide_=nullptr,warning_=nullptr;
  std::array<HWND,3> modes_{};
  std::array<HWND,5> sliders_{},labels_{},values_{};
  std::vector<HWND> home_controls_,settings_controls_;
  std::map<HWND,RECT> settings_positions_;
  HWND source_combo_=nullptr,capture_combo_=nullptr,output_combo_=nullptr,apply_=nullptr,settings_note_=nullptr;
  std::vector<EndpointInfo> source_list_,capture_list_,output_list_;
};
HWND App::Child(HWND parent,const wchar_t* kind,const wchar_t* text,DWORD style,int id){
  const auto h=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance_,nullptr);
  if(!h)throw std::runtime_error("cannot create desktop control");
  SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(parent==window_?body_:settings_body_),FALSE);
  if(parent==window_)home_controls_.push_back(h);else settings_controls_.push_back(h);
  return h;
}
void App::Text(HWND h,const std::wstring& text){
  const int count=GetWindowTextLengthW(h);std::wstring current(static_cast<std::size_t>(count)+1,0);
  GetWindowTextW(h,current.data(),count+1);current.resize(count);
  if(current!=text)SetWindowTextW(h,text.c_str());
}
void App::Theme(){
  DWORD light=1,size=sizeof(light);
  RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&size);
  HIGHCONTRASTW contrast{sizeof(contrast)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0);
  dark_=light==0;
  colors_=dark_?Palette{RGB(23,25,30),RGB(240,242,248),RGB(168,176,192),RGB(33,36,43),RGB(61,66,77),RGB(52,130,255)}
               :Palette{RGB(248,249,252),RGB(24,29,39),RGB(88,98,116),RGB(255,255,255),RGB(215,221,231),RGB(52,130,255)};
  if(contrast.dwFlags&HCF_HIGHCONTRASTON)colors_={GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),
      GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_WINDOW),GetSysColor(COLOR_WINDOWTEXT),GetSysColor(COLOR_HIGHLIGHT)};
  if(background_)DeleteObject(background_);if(panel_)DeleteObject(panel_);
  background_=CreateSolidBrush(colors_.bg);panel_=CreateSolidBrush(colors_.panel);
  const BOOL dark=dark_;if(window_)DwmSetWindowAttribute(window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
  if(settings_window_)DwmSetWindowAttribute(settings_window_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
  for(auto h:sliders_)if(h)SetWindowTheme(h,L"",L"");
  for(auto h:{source_combo_,capture_combo_,output_combo_})if(h)SetWindowTheme(h,L"",L"");
  if(window_)RedrawWindow(window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
  if(settings_window_)RedrawWindow(settings_window_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
void App::Fonts(bool settings){
  if(settings){
    const auto old_body=settings_body_,old_heading=settings_heading_;
    auto font=[&](int pixels,int weight){return CreateFontW(-MulDiv(pixels,static_cast<int>(settings_dpi_),96),0,0,0,weight,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
    settings_body_=font(15,FW_NORMAL);settings_heading_=font(18,FW_SEMIBOLD);
    for(auto h:settings_controls_)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(settings_body_),TRUE);
    if(!settings_controls_.empty())SendMessageW(settings_controls_[0],WM_SETFONT,reinterpret_cast<WPARAM>(settings_heading_),TRUE);
    if(old_body)DeleteObject(old_body);if(old_heading)DeleteObject(old_heading);return;
  }
  const auto old_body=body_,old_title=title_,old_heading=heading_;
  auto font=[&](int pixels,int weight){return CreateFontW(-Scale(pixels),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");};
  body_=font(15,FW_NORMAL);title_=font(27,FW_SEMIBOLD);heading_=font(18,FW_SEMIBOLD);
  for(auto h:home_controls_)SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(body_),TRUE);
  if(title_label_)SendMessageW(title_label_,WM_SETFONT,reinterpret_cast<WPARAM>(title_),TRUE);
  if(status_)SendMessageW(status_,WM_SETFONT,reinterpret_cast<WPARAM>(heading_),TRUE);
  if(old_body)DeleteObject(old_body);if(old_title)DeleteObject(old_title);if(old_heading)DeleteObject(old_heading);
}
void App::BuildHome(){
  title_label_=Child(window_,L"STATIC",L"CleanWave",SS_LEFT);
  subtitle_=Child(window_,L"STATIC",L"Live audio cleaner",SS_LEFT);
  settings_button_=Child(window_,L"BUTTON",L"Settings",BS_OWNERDRAW|WS_TABSTOP,Settings);
  status_=Child(window_,L"STATIC",L"Preparing CleanWave…",SS_LEFT);
  route_=Child(window_,L"STATIC",L"Checking selected devices",SS_LEFT);
  mode_label_=Child(window_,L"STATIC",L"Audio mode",SS_LEFT);
  for(int i=0;i<3;++i)modes_[i]=Child(window_,L"BUTTON",i==0?L"Off":i==1?L"Default":L"Custom",BS_OWNERDRAW|WS_TABSTOP,Off+i);
  mode_hint_=Child(window_,L"STATIC",L"Off passes original audio through the selected route.",SS_LEFT);
  custom_label_=Child(window_,L"STATIC",L"Personal mix",SS_LEFT);
  const wchar_t* names[]={L"Voice",L"Music",L"Background",L"Strength",L"Voice boost"};
  for(int i=0;i<5;++i){
    labels_[i]=Child(window_,L"STATIC",names[i],SS_LEFT);
    sliders_[i]=Child(window_,TRACKBAR_CLASSW,names[i],TBS_HORZ|TBS_NOTICKS|WS_TABSTOP,Voice+i);
    values_[i]=Child(window_,L"STATIC",L"0 dB",SS_RIGHT);
    SendMessageW(sliders_[i],TBM_SETRANGEMIN,FALSE,i<3?kSourceMixMinDb:0);
    SendMessageW(sliders_[i],TBM_SETRANGEMAX,FALSE,i<3?kSourceMixMaxDb:i==3?100:6);
    SendMessageW(sliders_[i],TBM_SETPAGESIZE,0,i==3?5:1);
  }
  start_stop_=Child(window_,L"BUTTON",L"Start bridge",BS_OWNERDRAW|WS_TABSTOP,StartStop);
  guide_=Child(window_,L"STATIC",L"Send your source app to CABLE Input.\nPause and restore its previous output before stopping the bridge.",SS_LEFT);
  warning_=Child(window_,L"STATIC",L"",SS_LEFT);
  Fonts();Theme();Layout();
}
void App::Place(HWND h,int x,int y,int width,int height,bool settings){
  if(settings){settings_positions_[h]={x,y,width,height};const auto scale=[&](int n){return MulDiv(n,static_cast<int>(settings_dpi_),96);};
    MoveWindow(h,scale(x),scale(y),scale(width),scale(height),TRUE);
  }else MoveWindow(h,Scale(x),Scale(y)-scroll_,Scale(width),Scale(height),TRUE);
}
void App::Layout(bool resize){
  if(!window_)return;
  const bool custom=preferences_.mode==ProcessingMode::Custom;
  content_height_=custom?800:548;
  if(resize && !IsZoomed(window_)){
    RECT client{};GetClientRect(window_,&client);RECT desired{0,0,client.right,Scale(content_height_)};
    AdjustWindowRectExForDpi(&desired,static_cast<DWORD>(GetWindowLongPtrW(window_,GWL_STYLE)),FALSE,0,dpi_);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window_,MONITOR_DEFAULTTONEAREST),&monitor);
    const int height=std::min(desired.bottom-desired.top,monitor.rcWork.bottom-monitor.rcWork.top-Scale(24));
    SetWindowPos(window_,nullptr,0,0,desired.right-desired.left,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  }
  RECT client{};GetClientRect(window_,&client);const int width=MulDiv(client.right,96,static_cast<int>(dpi_));
  SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE|SIF_POS};si.nMax=Scale(content_height_)-1;si.nPage=client.bottom;
  scroll_=std::clamp(scroll_,0,std::max(0,si.nMax-static_cast<int>(si.nPage)+1));si.nPos=scroll_;
  SetScrollInfo(window_,SB_VERT,&si,TRUE);
  Place(title_label_,88,23,width-270,36);Place(subtitle_,88,61,width-270,25);
  Place(settings_button_,width-136,28,104,36);
  Place(status_,48,116,width-96,28);Place(route_,48,150,width-96,48);
  Place(mode_label_,32,220,width-64,24);
  const int button_width=(width-88)/3;
  for(int i=0;i<3;++i)Place(modes_[i],32+i*(button_width+12),252,button_width,44);
  Place(mode_hint_,32,310,width-64,40);
  ShowWindow(custom_label_,custom?SW_SHOW:SW_HIDE);Place(custom_label_,32,360,width-64,26);
  for(int i=0;i<5;++i){
    for(auto h:{labels_[i],sliders_[i],values_[i]})ShowWindow(h,custom?SW_SHOW:SW_HIDE);
    Place(labels_[i],32,403+i*42,108,24);Place(sliders_[i],140,397+i*42,width-246,34);Place(values_[i],width-94,403+i*42,62,24);
  }
  const int footer=custom?625:368;
  Place(start_stop_,32,footer,214,42);Place(guide_,32,footer+56,width-64,45);Place(warning_,32,footer+104,width-64,50);
  if(settings_window_){
    const int w=588;
    // Settings controls are positioned at creation and scaled when DPI changes.
    Place(source_combo_,32,132,w-64,240,true);Place(capture_combo_,32,220,w-64,240,true);Place(output_combo_,32,308,w-64,240,true);
    Place(apply_,w-264,514,108,36,true);Place(GetDlgItem(settings_window_,Cancel),w-144,514,112,36,true);
    Place(GetDlgItem(settings_window_,Refresh),32,366,142,34,true);Place(settings_note_,32,414,w-64,84,true);
  }
  InvalidateRect(window_,nullptr,FALSE);
}
std::wstring App::EndpointName(const std::wstring& id) const{
  const auto it=std::find_if(snapshot_.endpoints.begin(),snapshot_.endpoints.end(),[&](const auto& e){return e.id==id;});
  return it==snapshot_.endpoints.end()?L"Saved device (unavailable)":it->name+(it->state==1?L"":L" (unavailable)");
}
void App::Sync(){
  snapshot_=controller_->Snapshot();
  if(!seeded_ && snapshot_.initialized){preferences_=snapshot_.preferences;seeded_=true;Layout(true);}
  if(seeded_ && snapshot_.applied_version>=submitted_)preferences_=snapshot_.preferences;
  Text(status_,snapshot_.status);
  Text(route_,L"Output: "+EndpointName(preferences_.route.physical_render_id)+L"\nInput: "+EndpointName(preferences_.route.capture_id));
  const int mode=static_cast<int>(preferences_.mode);
  for(int i=0;i<3;++i){Text(modes_[i],(i==0?L"Off":i==1?L"Default":L"Custom")+std::wstring(i==mode?L" (selected)":L""));
    EnableWindow(modes_[i],snapshot_.initialized && !closing_);InvalidateRect(modes_[i],nullptr,FALSE);}
  const wchar_t* hints[]={L"Original audio through the selected route. No AI processing.",
      L"Voice 0 dB · Music −10 dB · Background −20 dB",L"Your personal mix is kept when you choose Default or Off."};
  Text(mode_hint_,hints[mode]);
  const int positions[]={preferences_.custom.voice_db,preferences_.custom.music_db,preferences_.custom.other_db,
      preferences_.custom.reduction_percent,preferences_.custom.boost_db};
  for(int i=0;i<5;++i){SendMessageW(sliders_[i],TBM_SETPOS,TRUE,positions[i]);
    Text(values_[i],std::to_wstring(positions[i])+(i==3?L"%":L" dB"));EnableWindow(sliders_[i],snapshot_.initialized && !closing_);}
  const bool running=snapshot_.live.state==LiveState::Running;
  Text(start_stop_,running?L"Stop bridge":L"Start bridge");
  EnableWindow(start_stop_,snapshot_.initialized && !snapshot_.busy && !closing_);
  EnableWindow(settings_button_,snapshot_.initialized && !closing_);
  std::wstring warning;
  if(!snapshot_.error.empty())warning=L"Attention: "+Wide(snapshot_.error);
  else if(!snapshot_.persistence_warning.empty())warning=L"Settings could not be saved/loaded: "+Wide(snapshot_.persistence_warning);
  Text(warning_,warning);
  const bool visible=preferences_.mode==ProcessingMode::Custom;
  if((IsWindowVisible(custom_label_)!=FALSE)!=visible)Layout(true);
  SyncSettings();
}
void App::SetMode(ProcessingMode mode){
  preferences_.mode=mode;submitted_=controller_->Configure(preferences_);Layout(true);Sync();
}
void App::Mix(){
  int* values[]={&preferences_.custom.voice_db,&preferences_.custom.music_db,&preferences_.custom.other_db,
      &preferences_.custom.reduction_percent,&preferences_.custom.boost_db};
  for(int i=0;i<5;++i)*values[i]=static_cast<int>(SendMessageW(sliders_[i],TBM_GETPOS,0,0));
  submitted_=controller_->Configure(preferences_);Sync();
}
void App::FillCombo(HWND combo,const std::vector<EndpointInfo>& list,const std::wstring& selected){
  SendMessageW(combo,CB_RESETCONTENT,0,0);int index=-1;
  for(std::size_t i=0;i<list.size();++i){
    const auto text=list[i].name+(list[i].state==1?L"":L" (unavailable)");
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(list[i].id==selected)index=static_cast<int>(i);
  }
  SendMessageW(combo,CB_SETCURSEL,index,0);
}
std::wstring App::ComboId(HWND combo,const std::vector<EndpointInfo>& list){
  const auto index=SendMessageW(combo,CB_GETCURSEL,0,0);
  return index>=0 && static_cast<std::size_t>(index)<list.size()?list[index].id:L"";
}
void App::OpenSettings(){
  if(settings_window_){SetForegroundWindow(settings_window_);return;}
  settings_error_.clear();populated_endpoints_=0;
  RECT parent{};GetWindowRect(window_,&parent);RECT size{0,0,Scale(588),Scale(580)};
  AdjustWindowRectExForDpi(&size,WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME,dpi_);
  settings_window_=CreateWindowExW(WS_EX_DLGMODALFRAME,L"CleanWaveSettings",L"CleanWave Settings",WS_CAPTION|WS_SYSMENU,
      parent.left+Scale(20),parent.top+Scale(30),size.right-size.left,size.bottom-size.top,window_,nullptr,instance_,this);
  if(!settings_window_)throw std::runtime_error("cannot open Settings");
  settings_dpi_=GetDpiForWindow(settings_window_);Fonts(true);
  const auto title=Child(settings_window_,L"STATIC",L"Audio devices",SS_LEFT);Place(title,32,24,520,32,true);
  SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(settings_heading_),FALSE);
  const auto introduction=Child(settings_window_,L"STATIC",L"Select CleanWave's input and playback destination.",SS_LEFT);Place(introduction,32,66,520,30,true);
  const wchar_t* labels[]={L"Source apps should play to",L"Capture input",L"CleanWave output"};
  for(int i=0;i<3;++i){auto h=Child(settings_window_,L"STATIC",labels[i],SS_LEFT);Place(h,32,104+i*88,520,25,true);}
  source_combo_=Child(settings_window_,L"COMBOBOX",L"Source output",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,SourceChoice);
  capture_combo_=Child(settings_window_,L"COMBOBOX",L"Capture input",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,CaptureChoice);
  output_combo_=Child(settings_window_,L"COMBOBOX",L"Playback output",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,OutputChoice);
  Child(settings_window_,L"BUTTON",L"Refresh devices",BS_OWNERDRAW|WS_TABSTOP,Refresh);
  settings_note_=Child(settings_window_,L"STATIC",L"",SS_LEFT);
  apply_=Child(settings_window_,L"BUTTON",L"Apply",BS_OWNERDRAW|WS_TABSTOP,Apply);
  Child(settings_window_,L"BUTTON",L"Cancel",BS_OWNERDRAW|WS_TABSTOP,Cancel);
  EnableWindow(window_,FALSE);Theme();SyncSettings(true);Layout();ShowWindow(settings_window_,SW_SHOW);SetFocus(source_combo_);
}
void App::SyncSettings(bool populate){
  if(!settings_window_)return;
  if(populate || populated_endpoints_!=snapshot_.endpoints_version){
    auto source=ComboId(source_combo_,source_list_),capture=ComboId(capture_combo_,capture_list_),output=ComboId(output_combo_,output_list_);
    if(source.empty())source=preferences_.route.source_render_id;
    if(capture.empty())capture=preferences_.route.capture_id;
    if(output.empty())output=preferences_.route.physical_render_id;
    populated_endpoints_=snapshot_.endpoints_version;
    source_list_.clear();capture_list_.clear();output_list_.clear();
    for(const auto& e:snapshot_.endpoints){
      if(e.flow==EndpointFlow::Render && e.name.find(L"CABLE Input")!=std::wstring::npos)source_list_.push_back(e);
      if(e.flow==EndpointFlow::Capture && e.name.find(L"CABLE Output")!=std::wstring::npos)capture_list_.push_back(e);
      if(e.flow==EndpointFlow::Render && (e.state==1 || e.id==preferences_.route.physical_render_id) &&
         (e.adapter_instance_id.starts_with(L"USB\\") || e.adapter_instance_id.starts_with(L"HDAUDIO\\")))output_list_.push_back(e);
    }
    FillCombo(source_combo_,source_list_,source);
    FillCombo(capture_combo_,capture_list_,capture);FillCombo(output_combo_,output_list_,output);
  }
  const bool running=snapshot_.live.state==LiveState::Running;
  const bool editable=!running && !snapshot_.busy && !closing_ && settings_submitted_==0;
  for(auto h:{source_combo_,capture_combo_,output_combo_,apply_})EnableWindow(h,editable);
  Text(settings_note_,running?L"Stop the bridge before changing devices.\nYour source app's output is managed in Windows Volume mixer.":
      L"Set your source app's output to CABLE Input in Windows Volume mixer.\nPause and restore its prior output before closing the bridge.");
  if(!settings_error_.empty())Text(settings_note_,settings_error_);
  if(settings_submitted_ && snapshot_.applied_version>=settings_submitted_){
    const bool accepted=preferences_.route.physical_render_id==ComboId(output_combo_,output_list_) &&
        preferences_.route.capture_id==ComboId(capture_combo_,capture_list_) && preferences_.route.source_render_id==ComboId(source_combo_,source_list_);
    settings_submitted_=0;
    if(accepted){DestroyWindow(settings_window_);return;}
    settings_error_=L"Selection was not applied: "+Wide(snapshot_.error);Text(settings_note_,settings_error_);
  }
}
void App::DrawButton(const DRAWITEMSTRUCT& item){
  const int id=static_cast<int>(item.CtlID);const bool selected=id>=Off && id<=Custom && id-Off==static_cast<int>(preferences_.mode);
  const bool primary=selected || id==StartStop || id==Apply;
  auto brush=CreateSolidBrush(primary?colors_.accent:colors_.panel);auto pen=CreatePen(PS_SOLID,1,primary?colors_.accent:colors_.border);
  const auto old_b=SelectObject(item.hDC,brush),old_p=SelectObject(item.hDC,pen);
  RoundRect(item.hDC,item.rcItem.left,item.rcItem.top,item.rcItem.right,item.rcItem.bottom,Scale(10),Scale(10));
  SelectObject(item.hDC,old_b);SelectObject(item.hDC,old_p);DeleteObject(brush);DeleteObject(pen);
  SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,(item.itemState&ODS_DISABLED)?colors_.muted:primary?RGB(255,255,255):colors_.text);
  SelectObject(item.hDC,GetParent(item.hwndItem)==settings_window_?settings_body_:body_);std::wstring text;
  if(id>=Off && id<=Custom)text=id==Off?L"Off":id==Default?L"Default":L"Custom";
  else{const int count=GetWindowTextLengthW(item.hwndItem);text.resize(count+1);GetWindowTextW(item.hwndItem,text.data(),count+1);text.resize(count);}
  RECT r=item.rcItem;DrawTextW(item.hDC,text.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  if(item.itemState&ODS_FOCUS){InflateRect(&r,-Scale(4),-Scale(4));DrawFocusRect(item.hDC,&r);}
}
void App::Paint(HWND window){
  PAINTSTRUCT ps{};const auto dc=BeginPaint(window,&ps);RECT client{};GetClientRect(window,&client);FillRect(dc,&client,background_);
  if(window==window_){
    DrawIconEx(dc,Scale(32),Scale(29)-scroll_,icon_,Scale(42),Scale(42),0,nullptr,DI_NORMAL);
    const auto pen=CreatePen(PS_SOLID,1,colors_.border);const auto old_p=SelectObject(dc,pen),old_b=SelectObject(dc,panel_);
    RoundRect(dc,Scale(32),Scale(102)-scroll_,client.right-Scale(32),Scale(204)-scroll_,Scale(14),Scale(14));
    SelectObject(dc,old_b);SelectObject(dc,old_p);DeleteObject(pen);
  }
  EndPaint(window,&ps);
}
void App::Close(){
  if(closing_)return;closing_=true;Text(status_,L"Stopping audio before closing…");
  for(auto h:home_controls_)EnableWindow(h,FALSE);
  if(settings_window_)DestroyWindow(settings_window_);
  controller_->Shutdown();
}
LRESULT App::Handle(HWND h,UINT m,WPARAM w,LPARAM l,bool settings){
  switch(m){
    case WM_PAINT:Paint(h);return 0;
    case WM_ERASEBKGND:return 1;
    case WM_DRAWITEM:DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:case WM_CTLCOLORBTN:{
      const auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,colors_.text);SetBkColor(dc,colors_.bg);
      if(reinterpret_cast<HWND>(l)==status_ || reinterpret_cast<HWND>(l)==route_){SetBkColor(dc,colors_.panel);return reinterpret_cast<LRESULT>(panel_);}
      return reinterpret_cast<LRESULT>(background_);
    }
    case WM_NOTIFY:{
      const auto header=reinterpret_cast<NMHDR*>(l);
      if(header->code==NM_CUSTOMDRAW && header->idFrom>=Voice && header->idFrom<=Boost){
        const auto draw=reinterpret_cast<NMCUSTOMDRAW*>(l);
        if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
        if(draw->dwDrawStage==CDDS_ITEMPREPAINT){
          const COLORREF color=draw->dwItemSpec==TBCD_THUMB?colors_.accent:colors_.border;
          const auto brush=CreateSolidBrush(color);FillRect(draw->hdc,&draw->rc,brush);DeleteObject(brush);return CDRF_SKIPDEFAULT;
        }
      }break;
    }
    case WM_HSCROLL:if(!settings && !closing_)Mix();return 0;
    case WM_COMMAND:{
      const int id=LOWORD(w);if(HIWORD(w)!=BN_CLICKED && id!=Apply)return 0;
      if(settings){
        if(id==Cancel){DestroyWindow(h);return 0;}
        if(id==Refresh){controller_->Refresh();SetTimer(h,2,350,nullptr);return 0;}
        if(id==Apply){
          settings_error_.clear();
          auto candidate=preferences_;
          candidate.route={ComboId(source_combo_,source_list_),ComboId(capture_combo_,capture_list_),ComboId(output_combo_,output_list_)};
          settings_submitted_=controller_->Configure(candidate,true);submitted_=settings_submitted_;
          Text(settings_note_,L"Checking selected devices…");EnableWindow(apply_,FALSE);return 0;
        }
      }else if(id>=Off && id<=Custom)SetMode(static_cast<ProcessingMode>(id-Off));
      else if(id==StartStop){if(snapshot_.live.state==LiveState::Running)controller_->Stop();else controller_->Start();}
      else if(id==Settings)OpenSettings();
      return 0;
    }
    case WM_TIMER:if(settings && w==2){KillTimer(h,2);SyncSettings(true);}return 0;
    case UpdateMessage:if(!settings){Sync();if(closing_)Text(status_,L"Stopping audio before closing…");}return 0;
    case ClosedMessage:if(!settings){controller_->Join();DestroyWindow(h);}return 0;
    case WM_CLOSE:if(settings)DestroyWindow(h);else Close();return 0;
    case WM_DESTROY:
      if(settings){settings_window_=nullptr;settings_submitted_=0;settings_controls_.clear();source_combo_=capture_combo_=output_combo_=nullptr;
        settings_positions_.clear();
        EnableWindow(window_,TRUE);if(!closing_){SetForegroundWindow(window_);SetFocus(settings_button_);}}
      else PostQuitMessage(0);return 0;
    case WM_SIZE:if(!settings)Layout();return 0;
    case WM_VSCROLL:if(!settings){
      SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,SB_VERT,&si);const int action=LOWORD(w);
      if(action==SB_THUMBTRACK)scroll_=si.nTrackPos;
      else if(action==SB_LINEUP)scroll_-=Scale(32);else if(action==SB_LINEDOWN)scroll_+=Scale(32);
      else if(action==SB_PAGEUP)scroll_-=static_cast<int>(si.nPage);else if(action==SB_PAGEDOWN)scroll_+=static_cast<int>(si.nPage);
      Layout();return 0;
    }break;
    case WM_MOUSEWHEEL:if(!settings){scroll_-=GET_WHEEL_DELTA_WPARAM(w)*Scale(48)/WHEEL_DELTA;Layout();return 0;}break;
    case WM_GETMINMAXINFO:if(!settings){const auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={Scale(600),Scale(380)};return 0;}break;
    case WM_DPICHANGED:if(settings){
      settings_dpi_=HIWORD(w);Fonts(true);const auto rect=reinterpret_cast<RECT*>(l);
      SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);
      for(const auto& [control,position]:settings_positions_){const auto scale=[&](LONG n){return MulDiv(n,static_cast<int>(settings_dpi_),96);};
        MoveWindow(control,scale(position.left),scale(position.top),scale(position.right),scale(position.bottom),TRUE);}
      return 0;
    }else{dpi_=HIWORD(w);Fonts();const auto rect=reinterpret_cast<RECT*>(l);
      SetWindowPos(h,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);Layout();return 0;}break;
    case WM_SETTINGCHANGE:case WM_THEMECHANGED:Theme();return 0;
  }
  return DefWindowProcW(h,m,w,l);
}
LRESULT CALLBACK App::MainProc(HWND h,UINT m,WPARAM w,LPARAM l){
  auto app=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(m==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));app->window_=h;}
  if(!app)return DefWindowProcW(h,m,w,l);
  try{return app->Handle(h,m,w,l,false);}catch(const std::exception& e){MessageBoxW(h,Wide(e.what()).c_str(),L"CleanWave",MB_OK|MB_ICONERROR);return 0;}
}
LRESULT CALLBACK App::SettingsProc(HWND h,UINT m,WPARAM w,LPARAM l){
  auto app=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(m==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
  if(!app)return DefWindowProcW(h,m,w,l);
  return app->Handle(h,m,w,l,true);
}
int App::Run(int show){
  WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=MainProc;cls.hInstance=instance_;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
  icon_=LoadIconW(instance_,MAKEINTRESOURCEW(1));cls.hIcon=icon_;cls.hIconSm=icon_;cls.lpszClassName=L"CleanWaveDesktop";
  if(!RegisterClassExW(&cls))throw std::runtime_error("cannot register desktop window");
  cls.lpfnWndProc=SettingsProc;cls.lpszClassName=L"CleanWaveSettings";RegisterClassExW(&cls);
  HWND h=CreateWindowExW(0,L"CleanWaveDesktop",L"CleanWave",WS_OVERLAPPEDWINDOW|WS_VSCROLL,
      CW_USEDEFAULT,CW_USEDEFAULT,660,540,nullptr,nullptr,instance_,this);
  if(!h)throw std::runtime_error("cannot create CleanWave window");
  dpi_=GetDpiForWindow(h);Theme();Fonts();BuildHome();
  const auto fallback=ReadPreparedRoute(root_/L"docs/route.json");
  controller_=std::make_unique<DesktopController>(settings_path_,fallback,[this](const auto& p){
      auto diagnostic=settings_path_;diagnostic+=L".live-status.json";
      return std::make_unique<NativeDesktopBackend>(root_,p,std::move(diagnostic));},
      [h](bool closed){PostMessageW(h,closed?ClosedMessage:UpdateMessage,0,0);});
  Sync(); // controls stay disabled until off-thread initialization has completed
  ShowWindow(h,show);UpdateWindow(h);
  MSG message{};
  while(GetMessageW(&message,nullptr,0,0)>0){
    HWND dialog=settings_window_?settings_window_:h;
    if(!IsDialogMessageW(dialog,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
  }
  controller_.reset();DeleteObject(body_);DeleteObject(title_);DeleteObject(heading_);DeleteObject(settings_body_);DeleteObject(settings_heading_);DeleteObject(background_);DeleteObject(panel_);
  return static_cast<int>(message.wParam);
}
std::filesystem::path FindRoot(){
  std::array<wchar_t,32768> executable{};const auto size=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
  if(!size || size==executable.size())throw std::runtime_error("cannot locate executable");
  auto path=std::filesystem::path(executable.data()).parent_path();
  for(int i=0;i<8;++i){if(std::filesystem::exists(path/L"assets/engine/separator.tflite") &&
      std::filesystem::exists(path/L"docs/route.json"))return path;path=path.parent_path();}
  throw std::runtime_error("CleanWave assets/route are missing next to the project");
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
  HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\CleanWaveWindowsDesktop");
  if(!singleton)return 1;
  if(GetLastError()==ERROR_ALREADY_EXISTS){MessageBoxW(nullptr,L"CleanWave is already open.",L"CleanWave",MB_OK);CloseHandle(singleton);return 0;}
  try{
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto root=FindRoot();auto settings=root/L"runtime/desktop-settings.ini";
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(count==3 && std::wstring(args[1])==L"--settings")settings=args[2];
    else if(count!=1){LocalFree(args);throw std::runtime_error("usage: CleanWave.exe [--settings absolute-path]");}
    LocalFree(args);
    App app(instance,root,settings);const auto result=app.Run(show);CloseHandle(singleton);return result;
  }catch(const std::exception& e){MessageBoxW(nullptr,Wide(e.what()).c_str(),L"CleanWave",MB_OK|MB_ICONERROR);CloseHandle(singleton);return 1;}
}
