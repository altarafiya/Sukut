#include "sukut-host/TrayPopup.h"
#include <commctrl.h>
#include <psapi.h>
#include <windowsx.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <fstream>

namespace sukut::host {
namespace {
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
SIZE_T PrivateBytes(){PROCESS_MEMORY_COUNTERS_EX value{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&value),sizeof(value));return value.PrivateUsage;}
}
// The production controls/renderers run in a hidden window. No foreground
// activation, physical input, audio engine, route changes or working-set trim.
struct TrayPopupTests {
  static LRESULT CALLBACK KeepHidden(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
    // DefWindowProc's WM_SETREDRAW(TRUE) adds WS_VISIBLE. The production
    // popup is visible already; this fixture must remain hidden throughout.
    return message==WM_SETREDRAW?0:DefSubclassProc(window,message,w,l);
  }
  static void Open(TrayPopup& popup,bool dark=true,bool arabic=false,int count=40,UINT dpi=96) {
    popup.model_={};popup.model_.dark=dark;popup.model_.arabic=arabic;
    popup.model_.mode=3;popup.model_.status=L"Active · Strict";
    popup.model_.apps.push_back({"0",L"All system audio"});
    for(int i=1;i<count;++i)popup.model_.apps.push_back({std::to_string(i),L"Source app "+std::to_wstring(i)});
    popup.accent_=popup.Accent();Check(popup.InitializeWindow(),"hidden popup creation failed");
    SetWindowSubclass(popup.hwnd_,KeepHidden,19,0);
    popup.dpi_=dpi;SendMessageW(popup.list_,LB_SETITEMHEIGHT,0,popup.Px(36));
    popup.ApplyTexts();popup.Layout();popup.Position();popup.Expand(6);
    Check(!IsWindowVisible(popup.hwnd_),"test popup became visible");
  }
  static void Render(TrayPopup& popup,const std::filesystem::path& output={}) {
    RECT box{};GetClientRect(popup.list_,&box);HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),box.right,-box.bottom,1,32,BI_RGB,0,0,0,0,0};
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HGDIOBJ previous=SelectObject(dc,bitmap);
    const auto geometry=popup.ScrollGeometry();const int top=static_cast<int>(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)),row_height=popup.Px(36);
    for(int row=0;row<geometry.rows;++row) {
      DRAWITEMSTRUCT item{};item.CtlID=201;item.hwndItem=popup.list_;item.hDC=dc;item.itemID=top+row;
      item.rcItem={0,row*row_height,box.right,(row+1)*row_height};popup.DrawListItem(item);
    }
    popup.PaintScrollBar(dc);
    if(geometry.Visible()) {
      const int x=popup.model_.arabic?popup.Px(6):box.right-popup.Px(6),y=popup.Px(4)+geometry.offset+geometry.thumb/2;
      COLORREF thumb=GetPixel(dc,x,y),track=GetPixel(dc,x,box.bottom-1);
      Check(thumb!=CLR_INVALID && thumb!=track,"scroll thumb was not drawn");
      Check(popup.model_.dark?GetRValue(track)<80:GetRValue(track)>200,"scroll track theme is incorrect");
    }
    if(!output.empty()) {
      BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+box.right*box.bottom*4;
      std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<const char*>(pixels),box.right*box.bottom*4);
      Check(file.good(),"render artifact could not be saved");
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
  }
  // Whole flyout through WM_PRINT (window stays hidden), for visual review of the phone layout.
  static void RenderWhole(TrayPopup& popup,const std::filesystem::path& output) {
    if(popup.expanded_>=0)popup.Expand(popup.expanded_);
    RECT box{};GetClientRect(popup.hwnd_,&box);HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),box.right,-box.bottom,1,32,BI_RGB,0,0,0,0,0};
    void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);HGDIOBJ previous=SelectObject(dc,bitmap);
    SendMessageW(popup.hwnd_,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);
    for(HWND button:popup.buttons_){RECT r{};GetWindowRect(button,&r);MapWindowPoints(nullptr,popup.hwnd_,reinterpret_cast<POINT*>(&r),2);
      DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=GetDlgCtrlID(button);item.hwndItem=button;item.itemState=ODS_NOFOCUSRECT;
      SetViewportOrgEx(dc,r.left,r.top,nullptr);item.hDC=dc;item.rcItem={0,0,r.right-r.left,r.bottom-r.top};popup.DrawButton(item);SetViewportOrgEx(dc,0,0,nullptr);}
    BITMAPFILEHEADER header{};header.bfType=0x4D42;header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);header.bfSize=header.bfOffBits+box.right*box.bottom*4;
    std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));file.write(static_cast<const char*>(pixels),box.right*box.bottom*4);
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
  }
  static void Geometry() {
    Check(!TrayScroll::Calculate(5,180,36,24,0).Visible(),"short list has a scrollbar");
    auto top=TrayScroll::Calculate(40,180,36,24,0),bottom=TrayScroll::Calculate(40,180,36,24,35);
    Check(top.Visible() && top.rows==5 && top.maximum==35,"overflow geometry incorrect");
    Check(top.offset==0 && bottom.offset+bottom.thumb==bottom.track,"thumb misses an endpoint");
    Check(top.TopFromOffset(-100)==0 && top.TopFromOffset(10000)==35,"drag endpoints are not clamped");
    for(int n=0;n<=35;++n){auto g=TrayScroll::Calculate(40,180,36,24,n);Check(g.TopFromOffset(g.offset)==n,"drag mapping loses a row");}
    Check(!TrayScroll::Calculate(50,0,0,24,0).Visible(),"zero-height viewport has a scrollbar");
    auto huge=TrayScroll::Calculate(INT_MAX,360,72,48,INT_MAX);
    Check(huge.thumb>=48 && huge.TopFromOffset(huge.track)==huge.maximum,"large list overflows scrollbar arithmetic");
  }
  static void Run(const std::filesystem::path& output) {
    if(!output.empty())std::filesystem::create_directories(output);
    Geometry();std::string selected;
    TrayPopup popup(GetModuleHandleW(nullptr),{[](int){},[]{},[](int){},[](const auto&){},[&](const auto& id){selected=id;}});
    int refreshes=0;popup.actions_.refresh_apps=[&]{++refreshes;};
    Check(std::wstring(popup.BufferGlyph(0))!=popup.BufferGlyph(1) && std::wstring(popup.BufferGlyph(1))!=popup.BufferGlyph(2),"buffering icon does not follow all three levels");
    int background_commits=0,last_background=0;
    popup.actions_.set_background=[&](int value){++background_commits;last_background=value;};
    for(bool dark:{false,true})for(bool arabic:{false,true})for(UINT dpi:{96U,144U,192U}) {
      Open(popup,dark,arabic,3,dpi);
      auto model=popup.model_;model.mode=2;model.optimized_background_db=-9;popup.Update(model);
      Check(popup.background_slider_ && SendMessageW(popup.background_slider_,TBM_GETRANGEMIN,0,0)==-20 && SendMessageW(popup.background_slider_,TBM_GETRANGEMAX,0,0)==10,"optimized slider missing or range changed");
      Check(SendMessageW(popup.background_slider_,TBM_GETPOS,0,0)==-9,"slider did not use persisted background level");
      const auto commits=background_commits;
      SendMessageW(popup.background_slider_,TBM_SETPOS,TRUE,-14);
      SendMessageW(popup.hwnd_,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(popup.background_slider_));
      Check(background_commits==commits,"drag sent intermediate audio commands");
      model.status=L"Updated while dragging";popup.Update(model);
      Check(SendMessageW(popup.background_slider_,TBM_GETPOS,0,0)==-14,"status poll reset an in-progress slider gesture");
      SendMessageW(popup.hwnd_,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(popup.background_slider_));
      Check(background_commits==commits+1 && last_background==-14,"release did not commit once");
      SendMessageW(popup.hwnd_,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(popup.background_slider_));
      Check(background_commits==commits+1,"capture/key-up duplicated the commit");
      SendMessageW(popup.background_slider_,WM_KEYDOWN,VK_END,0);
      Check(background_commits==commits+1,"keyboard repeat committed before key-up");
      SendMessageW(popup.background_slider_,WM_KEYUP,VK_END,0);
      Check(last_background==10 && background_commits==commits+2,"native End key did not commit maximum");
      SendMessageW(popup.background_slider_,WM_KEYDOWN,VK_HOME,0);SendMessageW(popup.background_slider_,WM_KEYUP,VK_HOME,0);
      Check(last_background==-20 && background_commits==commits+3,"native Home key did not commit minimum");
      model=popup.model_;model.mode=1;popup.Update(model);Check(!popup.background_slider_,"slider retained outside Optimized");
      model.mode=0;model.preferred=2;popup.Update(model);Check(popup.background_slider_,"Optimized background cannot be prepared while Off");
      popup.Close();Check(!popup.background_slider_,"slider handle retained after Close");
    }
    refreshes=0;
    Open(popup,true,false,3);Check(refreshes==1,"opening sources did not request a catalog refresh");
    popup.Expand(6);Check(refreshes==1,"closing sources refreshed the catalog");
    popup.Expand(5);Check(refreshes==1,"output expansion refreshed source apps");
    popup.Expand(6);Check(refreshes==2,"reopening sources did not refresh apps");popup.Close();
    for(bool dark:{false,true})for(bool arabic:{false,true})for(UINT dpi:{96U,144U,192U}) {
      Open(popup,dark,arabic,40,dpi);
      Check(!(GetWindowLongPtrW(popup.list_,GWL_STYLE)&WS_VSCROLL),"classic scrollbar remains enabled");
      Check(popup.ScrollGeometry().Visible(),"overflow scrollbar is missing");
      const auto maximum=popup.ScrollGeometry().maximum,rows=popup.ScrollGeometry().rows;
      Render(popup);popup.ScrollList(100000);Check(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)==maximum,"last app is unreachable");Render(popup);
      popup.ScrollList(0);popup.scroll_dragging_=true;popup.scroll_grab_=0;
      RECT drag_box{};GetClientRect(popup.list_,&drag_box);
      SendMessageW(popup.list_,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(popup.Px(6),drag_box.bottom));
      Check(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)==maximum,"thumb drag misses the last app");
      SendMessageW(popup.list_,WM_LBUTTONUP,0,0);Check(!popup.scroll_dragging_,"thumb drag did not end");
      if(!output.empty() && dpi==96)Render(popup,output/(std::string(dark?"dark":"light")+(arabic?"-rtl.bmp":"-ltr.bmp")));
      popup.ScrollList(0);SendMessageW(popup.list_,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-60)),0);
      Check(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)==0,"partial wheel delta was rounded early");
      SendMessageW(popup.list_,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-60)),0);
      UINT lines=0;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
      if(lines)Check(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)>0,"wheel scrolling failed");
      popup.ScrollList(0);RECT box{};GetClientRect(popup.list_,&box);
      const int rail=arabic?popup.Px(6):box.right-popup.Px(6);
      SendMessageW(popup.list_,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(rail,box.bottom-popup.Px(6)));
      Check(SendMessageW(popup.list_,LB_GETTOPINDEX,0,0)==rows,"track click did not scroll one page");
      SendMessageW(popup.list_,WM_KEYDOWN,VK_END,0);
      Check(selected=="39","keyboard selection did not choose the last source");
      popup.Close();Check(!popup.IsOpen() && !popup.list_ && !popup.target_ && popup.formats_.empty() && popup.model_.apps.empty(),"popup retains resources after close");
    }
    Open(popup,true,false,3);Check(!popup.ScrollGeometry().Visible(),"short source list has a scrollbar");Render(popup);popup.Close();
    if(!output.empty()) {
      struct Look{bool dark,arabic;int mode;bool standby;const char* name;};
      for(const Look look:{Look{true,false,3,false,"flyout-dark-ltr-on"},Look{true,false,2,false,"flyout-dark-ltr-optimized"},Look{true,true,0,false,"flyout-dark-rtl-off"},
                           Look{false,false,1,true,"flyout-light-ltr-standby"},Look{false,true,2,false,"flyout-light-rtl-on"}}) {
        Open(popup,look.dark,look.arabic,3);popup.model_.mode=look.mode;popup.model_.preferred=2;popup.model_.standby=look.standby;
        popup.model_.status=look.mode==0?L"Off":look.standby?L"Standby · nothing playing":L"Active";popup.model_.buffering=1;
        popup.model_.outputs.push_back({"spk",L"Speakers (Realtek(R) Audio)"});popup.model_.output="spk";
        popup.ApplyTexts();popup.Layout();popup.Position();RenderWhole(popup,output/(std::string(look.name)+".bmp"));popup.Close();
      }
    }
    for(int n=0;n<4;++n){Open(popup);Render(popup);popup.Close();}
    const auto before=PrivateBytes();const DWORD gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int n=0;n<60;++n){Open(popup);auto model=popup.model_;model.mode=2;popup.Update(model);Render(popup);popup.Close();}
    const auto after=PrivateBytes();
    for(int n=0;n<60;++n){Open(popup);auto model=popup.model_;model.mode=2;popup.Update(model);Render(popup);popup.Close();}
    const auto final=PrivateBytes();
    std::cout<<"MEMORY before="<<before<<", after60="<<after<<", after120="<<final<<", GDI="<<GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<<", USER="<<GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)<<std::endl;
    Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==gdi,"popup GDI handles accumulate");
    Check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==user,"popup USER handles accumulate");
    Check(final<=before+8*1024*1024 && final<=after+2*1024*1024,"popup private memory keeps growing");
    std::cout<<"PASS slim scrollbar: dark/light, RTL/LTR, 100/150/200% DPI, wheel, page click, keyboard selection and all rows reachable\n";
    std::cout<<"PASS buffering glyphs, native Optimized slider -20..+10 dB, no commands during drag, one release commit and resource cleanup\n";
    std::cout<<"PASS 120 open/render/close cycles; GDI="<<gdi<<", USER="<<user<<", private bytes before="<<before<<", after60="<<after<<", after120="<<final<<"\n";
  }
};
}
int main(int count,char** args){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);try{sukut::host::TrayPopupTests::Run(count>1?std::filesystem::path(args[1]):std::filesystem::path());return 0;}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
