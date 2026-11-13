#include "sukut-host/TrayPopup.h"
#include "sukut-host/TrayCatalog.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace sukut::host {
namespace {
constexpr wchar_t kClass[] = L"SukutTrayPopup";
constexpr int kFirstId = 100, kListId = 201, kBackgroundId = 202;
// Header with an On/Off switch, three mode tiles, three playback rows and Open Sukut.
constexpr float kWidth = 336, kBaseHeight = 366, kTileHeight = 64, kRowHeight = 44;
constexpr std::array<const wchar_t*, 4> kModeGlyph{L"", L"", L"", L""};
template <typename T> void SafeRelease(T*& v) { if (v) { v->Release(); v = nullptr; } }

// The phone's palette (Android UiPalette) on Fluent flyout surfaces.
struct Palette {
  UINT32 background, card, card_hover, stroke, ink, secondary, accent, on_accent, accent_container,
      warning, warning_container, list, list_hover, list_selected;
};
Palette PaletteFor(bool dark) {
  if (dark) return {0x2C2C2C, 0x383838, 0x414141, 0x464646, 0xFFFFFF, 0xC5C5C5, 0x80BFEA, 0x000000, 0x303D45,
                    0xE3C184, 0x3B3324, 0x303030, 0x3C3C3C, 0x303D45};
  return {0xF9F9F9, 0xFFFFFF, 0xF3F3F3, 0xE3E3E3, 0x1A1A1A, 0x5D5D5D, 0x17659B, 0xFFFFFF, 0xDEE6EB,
          0x805B20, 0xF4ECD9, 0xFFFFFF, 0xF0F0F0, 0xE6EEF4};
}
std::wstring ModeLabel(int mode, bool ar) {
  static const std::array<const wchar_t*, 4> en{L"Off", L"Music Filter", L"Optimized", L"Strict"};
  static const std::array<const wchar_t*, 4> arabic{L"إيقاف", L"مرشّح الموسيقى", L"محسّن", L"صارم"};
  return (ar ? arabic : en)[std::clamp(mode, 0, 3)];
}
std::wstring BufferLabel(int level, bool ar) {
  static const std::array<const wchar_t*, 3> en{L"Lowest", L"Balanced", L"Maximum"};
  static const std::array<const wchar_t*, 3> arabic{L"الأقل", L"متوازن", L"الأكبر"};
  return (ar ? arabic : en)[std::clamp(level, 0, 2)];
}
std::wstring SelectedName(const std::vector<TrayPopup::Choice>& choices, const std::string& id, bool ar) {
  for (const auto& choice : choices) if (choice.id == id) return choice.name;
  return ar ? L"جارٍ التحديث…" : L"Refreshing…";
}
// The tile shown as selected: the running mode, or while Off the one Power turns on.
int SelectedTile(const TrayPopup::Model& m) { return m.mode != 0 ? m.mode : std::clamp(m.preferred, 1, 3); }
}

TrayPopup::TrayPopup(HINSTANCE instance, Actions actions) : instance_(instance), actions_(std::move(actions)) {
  WNDCLASSEXW wc{sizeof(wc)};
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = Procedure; wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
}
TrayPopup::~TrayPopup() { actions_.closed = nullptr; Close(); }
D2D1_COLOR_F TrayPopup::Color(UINT32 rgb, float alpha) const {
  return D2D1::ColorF(((rgb >> 16) & 255) / 255.0F, ((rgb >> 8) & 255) / 255.0F, (rgb & 255) / 255.0F, alpha);
}
UINT32 TrayPopup::Accent() const {
  return PaletteFor(model_.dark).accent;
}
const wchar_t* TrayPopup::BufferGlyph(int level) {
  static constexpr const wchar_t* glyphs[]{L"\uE945",L"\uE9E9",L"\uE823"};
  return glyphs[std::clamp(level,0,2)];
}
UINT32 TrayPopup::BufferColor(int level,bool dark) {
  // Same semantic palette as the desktop's buffering tiles.
  static constexpr UINT32 night[]{0xB9AEFF,0xE3C184,0xF2A7AB}, day[]{0x6550A5,0x805B20,0xA02C3B};
  return (dark?night:day)[std::clamp(level,0,2)];
}

void TrayPopup::Show(const RECT& anchor, const Model& model) {
  if (hwnd_) { Update(model); SetForegroundWindow(hwnd_); return; }
  model_ = model; anchor_ = anchor; expanded_ = -1; expansion_height_ = 0; height_ = kBaseHeight;
  accent_ = Accent(); accent_text_ = PaletteFor(model_.dark).on_accent;
  if(!InitializeWindow())return;
  ApplyTexts(); Layout(); Position(true);
  SetForegroundWindow(hwnd_);
  SetFocus(buttons_[model_.mode == 0 ? 0 : std::clamp(model_.mode, 1, 3)]);
  SendMessageW(hwnd_, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
}

bool TrayPopup::InitializeWindow() {
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"Sukut", WS_POPUP | WS_CLIPCHILDREN,
                         anchor_.left, anchor_.top, 10, 10, nullptr, nullptr, instance_, this);
  if (!hwnd_) return false;
  dpi_ = GetDpiForWindow(hwnd_); if (!dpi_) dpi_ = 96;
  const DWM_WINDOW_CORNER_PREFERENCE round = DWMWCP_ROUND;
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
  const BOOL dark = model_.dark;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
  const COLORREF border = model_.dark ? RGB(71, 71, 71) : RGB(218, 218, 218);
  DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
  background_brush_ = CreateSolidBrush(model_.dark ? RGB(44, 44, 44) : RGB(249, 249, 249));
  for (int i = 0; i < 8; ++i) {
    buttons_[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFirstId + i)), instance_, nullptr);
    SetWindowSubclass(buttons_[i], ButtonProcedure, i, reinterpret_cast<DWORD_PTR>(this));
  }
  list_ = CreateWindowExW(model_.arabic ? WS_EX_RTLREADING : 0, L"LISTBOX", L"",
      WS_CHILD | WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
      0, 0, 10, 10, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), instance_, nullptr);
  SetWindowSubclass(list_,ListProcedure,1,reinterpret_cast<DWORD_PTR>(this));
  SendMessageW(list_, LB_SETITEMHEIGHT, 0, Px(36));
  CreateResources(); return true;
}

void TrayPopup::Position(bool animate) {
  MONITORINFO monitor{sizeof(monitor)};
  GetMonitorInfoW(MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST), &monitor);
  const RECT work = monitor.rcWork;
  const int w = Px(kWidth), h = Px(height_), margin = Px(8);
  const auto bound = [](int v, int lo, int hi) { return std::clamp(v, lo, std::max(lo, hi)); };
  const int cx = (anchor_.left + anchor_.right) / 2, cy = (anchor_.top + anchor_.bottom) / 2;
  final_x_ = bound(cx - w / 2, work.left + margin, work.right - w - margin);
  final_y_ = work.bottom - h - margin;
  if (anchor_.bottom <= work.top) final_y_ = work.top + margin;
  else if (anchor_.left >= work.right || anchor_.right <= work.left) {
    final_x_ = anchor_.left >= work.right ? work.right - w - margin : work.left + margin;
    final_y_ = bound(cy - h / 2, work.top + margin, work.bottom - h - margin);
  }
  final_y_ = std::max(static_cast<int>(work.top) + margin, final_y_);
  SetWindowPos(hwnd_, HWND_TOPMOST, final_x_, final_y_, w, h, SWP_NOACTIVATE | (animate?SWP_SHOWWINDOW:SWP_NOREDRAW));
  if(animate)RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
}

void TrayPopup::Update(Model model) {
  if (model == model_) return;
  const bool appearance=model.mode!=model_.mode || model.dark!=model_.dark;
  const bool resize=(SelectedTile(model)==2)!=((SelectedTile(model_)==2 && !model_.advanced_optimized));
  const bool changed = model.outputs != model_.outputs || model.apps != model_.apps || model.buffering != model_.buffering || model.output != model_.output || model.source_pid != model_.source_pid;
  model_ = std::move(model);
  accent_=Accent(); accent_text_=PaletteFor(model_.dark).on_accent;
  if (!hwnd_) return;
  ApplyTexts();
  for (int i = 0; i < 8; ++i) EnableWindow(buttons_[i], i == 4 || !model_.busy);
  EnableWindow(list_, !model_.busy);
  if (changed && expanded_ >= 0) PopulateList();
  if (resize || changed && expanded_ >= 0) { Layout(); Position(); }
  if(background_slider_)InvalidateRect(background_slider_,nullptr,FALSE);
  if(appearance)InvalidateRect(list_,nullptr,FALSE);
  InvalidateRect(hwnd_, nullptr, FALSE);
  for (HWND b : buttons_) InvalidateRect(b, nullptr, FALSE);
}

void TrayPopup::ApplyTexts() {
  const bool ar = model_.arabic;
  for(int i=0;i<8;++i) EnableWindow(buttons_[i],i==4 || !model_.busy);
  EnableWindow(list_,!model_.busy);
  if(background_slider_) {
    EnableWindow(background_slider_,!model_.busy);
    SetWindowTextW(background_slider_,ar?L"مستوى الخلفية، ديسيبل":L"Background level, dB");
    if(!background_tracking_) {
      background_preview_=std::clamp(model_.optimized_background_db,-20,10);
      SendMessageW(background_slider_,TBM_SETPOS,TRUE,background_preview_);
    }
  }
  // Button 0 is the power button; 1-3 are the mode tiles.
  SetWindowTextW(buttons_[0], model_.mode == 0
      ? ((ar ? L"تشغيل سُكوت: " : L"Turn on Sukut: ") + ModeLabel(SelectedTile(model_), ar)).c_str()
      : (ar ? L"إيقاف سُكوت" : L"Turn off Sukut"));
  for (int i = 1; i < 4; ++i) SetWindowTextW(buttons_[i], (ModeLabel(i, ar) + (i == model_.mode ? (ar ? L"، محدد" : L", selected") : L"")).c_str());
  SetWindowTextW(buttons_[4], ar ? L"فتح سُكوت" : L"Open Sukut");
  SetWindowTextW(buttons_[5], ((ar ? L"جهاز الصوت: " : L"Audio output: ") + SelectedName(model_.outputs, model_.output, ar)).c_str());
  SetWindowTextW(buttons_[6], ((ar ? L"التطبيق المصدر: " : L"Source app: ") + SelectedName(model_.apps, model_.source_pid, ar)).c_str());
  SetWindowTextW(buttons_[7], ((ar ? L"التخزين المؤقت للصوت: " : L"Audio buffering: ") + BufferLabel(model_.buffering,ar)).c_str());
  buffering_choices_ = {{"0",BufferLabel(0,ar)},{"1",BufferLabel(1,ar)},{"2",BufferLabel(2,ar)}};
  SetWindowTextW(list_,expanded_==5?(ar?L"جهاز الصوت":L"Audio output"):expanded_==6?(ar?L"التطبيق المصدر":L"Source app"):(ar?L"التخزين المؤقت للصوت":L"Audio buffering"));
  SetWindowTextW(hwnd_, ((ar ? std::wstring(L"سُكوت · ") : std::wstring(L"Sukut · ")) + model_.status).c_str());
}

const std::vector<TrayPopup::Choice>& TrayPopup::CurrentChoices() const {
  return expanded_==5?model_.outputs:expanded_==6?model_.apps:buffering_choices_;
}

void TrayPopup::Layout() {
  expansion_height_=0;
  if(expanded_>=0) {
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&anchor_,MONITOR_DEFAULTTONEAREST),&monitor);
    const float room=std::max(36.0F,(monitor.rcWork.bottom-monitor.rcWork.top)/Scale()-kBaseHeight-((SelectedTile(model_)==2 && !model_.advanced_optimized)?64:0)-20);
    expansion_height_=std::min({180.0F,std::max(1.0F,static_cast<float>(CurrentChoices().size()))*36.0F,room});
  }
  const auto place=[&](HWND control,float x,float y,float width,float height){
    SetWindowPos(control,nullptr,Px(model_.arabic?kWidth-x-width:x),Px(y),Px(width),Px(height),SWP_NOZORDER|SWP_NOACTIVATE);
  };
  place(buttons_[0],kWidth-14-56,14,56,34);                // On/Off switch, opposite the title
  modes_top_=82;                                           // "Audio mode" label sits above
  const float tile=(kWidth-28-12)/3;
  for(int i=1;i<4;++i)place(buttons_[i],14+(i-1)*(tile+6),modes_top_,tile,kTileHeight);
  playback_top_=modes_top_+kTileHeight+8;                  // "Playback" label sits here
  if((SelectedTile(model_)==2 && !model_.advanced_optimized)) {
    if(!background_slider_) {
      background_slider_=CreateWindowExW(model_.arabic?WS_EX_LAYOUTRTL:0,TRACKBAR_CLASSW,
          model_.arabic?L"مستوى الخلفية، ديسيبل":L"Background level, dB",
          WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,0,0,10,10,hwnd_,
          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackgroundId)),instance_,nullptr);
      SetWindowSubclass(background_slider_,SliderProcedure,1,reinterpret_cast<DWORD_PTR>(this));
      SendMessageW(background_slider_,TBM_SETRANGEMIN,FALSE,-20);
      SendMessageW(background_slider_,TBM_SETRANGEMAX,FALSE,10);
      SendMessageW(background_slider_,TBM_SETPAGESIZE,0,5);
      background_preview_=std::clamp(model_.optimized_background_db,-20,10);
      SendMessageW(background_slider_,TBM_SETPOS,TRUE,background_preview_);
      // Keep keyboard traversal between the mode tiles and Playback.
      SetWindowPos(background_slider_,buttons_[3],0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    background_top_=playback_top_+4;
    place(background_slider_,18,background_top_+20,kWidth-36,36);
    EnableWindow(background_slider_,!model_.busy);
    playback_top_+=64;
  } else if(background_slider_) {
    background_tracking_=false;
    RemoveWindowSubclass(background_slider_,SliderProcedure,1);DestroyWindow(background_slider_);background_slider_=nullptr;
  }
  float y=playback_top_+20;
  for(int section:{7,5,6}) {
    place(buttons_[section],14,y,kWidth-28,kRowHeight); y+=kRowHeight;
    if(expanded_==section){place(list_,14,y+2,kWidth-28,expansion_height_);y+=expansion_height_+4;}
    y+=4;
  }
  place(buttons_[4],14,y+4,kWidth-28,36);
  height_=y+4+36+12;
  ShowWindow(list_,expanded_>=0?SW_SHOWNA:SW_HIDE);
}

void TrayPopup::PopulateList() {
  list_hover_ = -1;scroll_hover_=false;wheel_remainder_=0;
  if(scroll_dragging_){scroll_dragging_=false;ReleaseCapture();}
  SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list_, LB_RESETCONTENT, 0, 0);
  const auto& choices = CurrentChoices();
  const std::string selected=expanded_==5?model_.output:expanded_==6?model_.source_pid:std::to_string(model_.buffering);
  int index = 0;
  for (const auto& choice : choices) {
    SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.name.c_str()));
    if (choice.id == selected) SendMessageW(list_, LB_SETCURSEL, index, 0);
    ++index;
  }
  SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list_, nullptr, FALSE);
}
void TrayPopup::Expand(int section) {
  SendMessageW(hwnd_,WM_SETREDRAW,FALSE,0);
  expanded_ = expanded_ == section ? -1 : section;
  if (expanded_ >= 0) PopulateList();
  ApplyTexts(); Layout(); Position();
  SendMessageW(hwnd_,WM_SETREDRAW,TRUE,0);
  RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN|RDW_FRAME);
  if (expanded_ >= 0) SetFocus(list_);
  if (expanded_ == 6 && actions_.refresh_apps) actions_.refresh_apps();
}

void TrayPopup::CreateResources() {
  D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_);
  DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwrite_));
  if (!d2d_) return;
  auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96, 96);
  d2d_->CreateDCRenderTarget(&props, &target_);
  if (target_) { target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE); target_->CreateSolidColorBrush(D2D1::ColorF(0), &brush_); }
}
void TrayPopup::ReleaseResources() {
  for(auto& f:formats_) { SafeRelease(f.ellipsis); SafeRelease(f.format); }
  formats_.clear(); formats_.shrink_to_fit();
  SafeRelease(brush_); SafeRelease(target_); SafeRelease(dwrite_); SafeRelease(d2d_);
}
void TrayPopup::Close() {
  if (!hwnd_) return;
  HWND window = hwnd_; hwnd_ = nullptr;
  background_tracking_=background_hover_=false;
  if(background_slider_)RemoveWindowSubclass(background_slider_,SliderProcedure,1);
  for (int i = 0; i < 8; ++i) if (buttons_[i]) RemoveWindowSubclass(buttons_[i], ButtonProcedure, i);
  if(list_) RemoveWindowSubclass(list_,ListProcedure,1);
  DestroyWindow(window);
  for (auto& b : buttons_) b = nullptr;
  list_ = nullptr; std::vector<Choice>().swap(buffering_choices_);
  background_slider_=nullptr;
  if (background_brush_) { DeleteObject(background_brush_); background_brush_ = nullptr; }
  ReleaseResources(); hover_ = list_hover_ = -1; expanded_ = -1; closed_at_ = GetTickCount64();
  scroll_hover_=scroll_dragging_=false;wheel_remainder_=0;
  // Choice strings belong only to the open flyout, not to a hidden GUI cache.
  model_ = {};
  if (actions_.closed) actions_.closed();
}

void TrayPopup::Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, const D2D1_RECT_F& box, UINT32 color,
                     DWRITE_TEXT_ALIGNMENT align, const wchar_t* family, float alpha) {
  if (!dwrite_ || !target_) return;
  const std::wstring face = family ? family : L"Segoe UI";
  auto found=std::find_if(formats_.begin(),formats_.end(),[&](const auto& f){return f.size==size && f.weight==weight && f.align==align && f.arabic==model_.arabic && f.family==face;});
  if(found==formats_.end()) {
    TextFormat f{size,weight,align,model_.arabic,face,nullptr,nullptr};
    if(FAILED(dwrite_->CreateTextFormat(face.c_str(),nullptr,weight,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,
        size*Scale(),model_.arabic?L"ar-sa":L"en-us",&f.format))) return;
    f.format->SetTextAlignment(model_.arabic && !family && align != DWRITE_TEXT_ALIGNMENT_CENTER ? DWRITE_TEXT_ALIGNMENT_LEADING : align);
    f.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if(model_.arabic && !family) f.format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
    DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0};
    if(SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(f.format,&f.ellipsis))) f.format->SetTrimming(&trimming,f.ellipsis);
    formats_.push_back(std::move(f)); found=std::prev(formats_.end());
  }
  auto* format=found->format;
  // DirectWrite's leading edge follows reading direction; RTL + trailing
  // would mirror a second time and put Arabic captions on the left.
  format->SetTextAlignment(model_.arabic && !family && align != DWRITE_TEXT_ALIGNMENT_CENTER ? DWRITE_TEXT_ALIGNMENT_LEADING : align);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  if (model_.arabic && !family) format->SetReadingDirection(DWRITE_READING_DIRECTION_RIGHT_TO_LEFT);
  brush_->SetColor(Color(color, alpha));
  target_->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format, box, brush_, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void TrayPopup::Paint() {
  PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd_,&ps); PaintToDc(dc); EndPaint(hwnd_,&ps);
}

void TrayPopup::PaintToDc(HDC dc) {
  RECT client; GetClientRect(hwnd_,&client);
  if (target_ && SUCCEEDED(target_->BindDC(dc, &client))) {
    const auto p = PaletteFor(model_.dark);
    const float s = Scale(), width = kWidth * s;
    const auto align = model_.arabic ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
    // The title and status take the side opposite the power button.
    const float start = model_.arabic ? 68*s : 18*s, end = model_.arabic ? width-18*s : width-68*s;
    target_->BeginDraw(); target_->Clear(Color(p.background));
    Text(model_.arabic ? L"سُكوت" : L"Sukut", 18, DWRITE_FONT_WEIGHT_SEMI_BOLD, D2D1::RectF(start, 10*s, end, 34*s), p.ink, align);
    brush_->SetColor(Color(model_.attention ? p.warning : model_.mode != 0 ? p.accent : p.secondary));
    const float dot = model_.arabic ? width-23*s : 23*s, inset = (model_.standby && !model_.attention ? 41 : 31)*s;
    if (model_.standby && !model_.attention) {
      // Standby (5 s with nothing playing): a small sleeping face, like the phone's.
      const float cy = 45*s, r = 7*s;
      target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dot, cy), r, r), brush_, 1.4F*s);
      target_->DrawLine(D2D1::Point2F(dot-4.2F*s, cy-1*s), D2D1::Point2F(dot-1.4F*s, cy-1*s), brush_, 1.3F*s);
      target_->DrawLine(D2D1::Point2F(dot+1.4F*s, cy-1*s), D2D1::Point2F(dot+4.2F*s, cy-1*s), brush_, 1.3F*s);
      target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(dot, cy+3*s), 1.2F*s, 1.3F*s), brush_, 1.1F*s);
      Text(L"z", 9, DWRITE_FONT_WEIGHT_BOLD, D2D1::RectF(dot+5*s, cy-17*s, dot+12*s, cy-8*s), p.accent, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe UI");
    } else {
      target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dot, 45*s), 3*s, 3*s), brush_);
    }
    Text(model_.status, 12, DWRITE_FONT_WEIGHT_NORMAL, D2D1::RectF(model_.arabic ? start : inset, 35*s, model_.arabic ? width-inset : end, 55*s), p.secondary, align);
    Text(model_.arabic ? L"وضع الصوت" : L"Audio mode", 12, DWRITE_FONT_WEIGHT_NORMAL, D2D1::RectF(18*s, (modes_top_-20)*s, width-18*s, (modes_top_-3)*s), p.secondary, align);
    if(background_slider_) {
      const auto value=background_tracking_?background_preview_:std::clamp(model_.optimized_background_db,-20,10);
      Text(model_.arabic?L"مستوى الخلفية":L"Background level",12,DWRITE_FONT_WEIGHT_NORMAL,
           D2D1::RectF(model_.arabic?88*s:18*s,background_top_*s,model_.arabic?width-18*s:width-88*s,(background_top_+20)*s),p.secondary,align);
      Text((value>0?L"+":L"")+std::to_wstring(value)+L" dB",12,DWRITE_FONT_WEIGHT_SEMI_BOLD,
           D2D1::RectF(model_.arabic?18*s:width-80*s,background_top_*s,model_.arabic?80*s:width-18*s,(background_top_+20)*s),p.accent,DWRITE_TEXT_ALIGNMENT_CENTER,L"Segoe UI");
    }
    Text(model_.arabic ? L"التشغيل" : L"Playback", 12, DWRITE_FONT_WEIGHT_NORMAL, D2D1::RectF(18*s, playback_top_*s, width-18*s, (playback_top_+17)*s), p.secondary, align);
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) { ReleaseResources(); CreateResources(); }
  }
}

void TrayPopup::DrawButton(const DRAWITEMSTRUCT& item) {
  if (!target_ || FAILED(target_->BindDC(item.hDC, &item.rcItem))) return;
  const auto p = PaletteFor(model_.dark);
  const int index = item.CtlID - kFirstId;
  const bool pressed = (item.itemState & ODS_SELECTED) != 0, hovered = hover_ == index || pressed;
  const bool focus = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
  const float s = Scale(), w = static_cast<float>(item.rcItem.right-item.rcItem.left), h = static_cast<float>(item.rcItem.bottom-item.rcItem.top);
  const float alpha = (item.itemState & ODS_DISABLED) ? .45F : 1;
  target_->BeginDraw(); target_->Clear(Color(p.background));
  D2D1_ROUNDED_RECT shape = D2D1::RoundedRect(D2D1::RectF(s, s, w-s, h-s), 8*s, 8*s);
  if (index == 0) {
    // Fluent toggle switch. Off: outlined track, small thumb at the start.
    // On: accent track, larger thumb at the end. Attention uses the warning colour.
    const bool on = model_.mode != 0, attention = model_.attention;
    const float tw = 40*s, th = 20*s, tx = (w-tw)/2, ty = (h-th)/2;
    shape = D2D1::RoundedRect(D2D1::RectF(tx, ty, tx+tw, ty+th), th/2, th/2);
    const UINT32 tone = attention ? p.warning : p.accent;
    if (on) { brush_->SetColor(Color(tone, alpha)); target_->FillRoundedRectangle(shape, brush_); }
    else {
      brush_->SetColor(Color(p.ink, (hovered ? .09F : .04F) * alpha)); target_->FillRoundedRectangle(shape, brush_);
      brush_->SetColor(Color(attention ? p.warning : p.secondary, alpha)); target_->DrawRoundedRectangle(shape, brush_, s);
    }
    if (on && hovered) { brush_->SetColor(Color(p.on_accent, pressed ? .16F : .08F)); target_->FillRoundedRectangle(shape, brush_); }
    // The thumb's end mirrors in Arabic, like Windows' own switches.
    const bool at_end = on != model_.arabic;
    const float radius = (on ? 7.0F : (hovered ? 6.0F : 5.0F)) * s;
    const float cx = at_end ? tx+tw-th/2 : tx+th/2;
    brush_->SetColor(Color(on ? p.on_accent : attention ? p.warning : p.secondary, alpha));
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, h/2), radius, radius), brush_);
  } else if (index < 4) {
    // Neutral tiles like the phone; the selected one gets the accent container and edge.
    const bool selected = index == SelectedTile(model_);
    shape = D2D1::RoundedRect(D2D1::RectF(s, s, w-s, h-s), 10*s, 10*s);
    brush_->SetColor(Color(selected ? p.accent_container : hovered ? p.card_hover : p.card, alpha)); target_->FillRoundedRectangle(shape, brush_);
    brush_->SetColor(Color(selected ? p.accent : p.stroke, alpha)); target_->DrawRoundedRectangle(shape, brush_, (selected ? 2.0F : 1.0F)*s);
    const UINT32 ink = selected ? p.accent : p.ink;
    Text(kModeGlyph[index], 17, DWRITE_FONT_WEIGHT_NORMAL, D2D1::RectF(0, 8*s, w, 31*s), ink, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets", alpha);
    if(index==1){brush_->SetColor(Color(ink,alpha));target_->DrawLine(D2D1::Point2F(w/2-9*s,10*s),D2D1::Point2F(w/2+9*s,29*s),brush_,1.6F*s);}
    Text(ModeLabel(index, model_.arabic), 12.5F, selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
         D2D1::RectF(4*s, 33*s, w-4*s, 56*s), ink, DWRITE_TEXT_ALIGNMENT_CENTER, nullptr, alpha);
  } else if (index >= 5) {
    // Playback rows: rounded cards with an icon, caption, value and chevron.
    brush_->SetColor(Color(hovered ? p.card_hover : p.card, alpha)); target_->FillRoundedRectangle(shape, brush_);
    brush_->SetColor(Color(p.stroke, alpha)); target_->DrawRoundedRectangle(shape, brush_, s);
    const auto align = model_.arabic ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
    const float left = (model_.arabic ? 30 : 40)*s, right = w-(model_.arabic ? 40 : 30)*s;
    const float icon_left=model_.arabic?w-32*s:12*s;
    const UINT32 icon = index==7 ? BufferColor(model_.buffering,model_.dark) : p.accent;
    Text(index==5?OutputGlyph(SelectedName(model_.outputs,model_.output,model_.arabic)):index==6?L"":BufferGlyph(model_.buffering),16,DWRITE_FONT_WEIGHT_NORMAL,D2D1::RectF(icon_left,0,icon_left+20*s,h),icon,DWRITE_TEXT_ALIGNMENT_CENTER,L"Segoe MDL2 Assets",alpha);
    Text(index==7?BufferLabel(model_.buffering,model_.arabic):SelectedName(index==5?model_.outputs:model_.apps,index==5?model_.output:model_.source_pid,model_.arabic),
         12.5F, DWRITE_FONT_WEIGHT_SEMI_BOLD, D2D1::RectF(left, 4*s, right, 24*s), index==7 ? icon : p.ink, align, nullptr, alpha);
    Text(index == 5 ? (model_.arabic ? L"جهاز الصوت" : L"Audio output") : index==6 ? (model_.arabic ? L"التطبيق المصدر" : L"Source app") : (model_.arabic ? L"التخزين المؤقت للصوت" : L"Audio buffering"),
         10.5F, DWRITE_FONT_WEIGHT_NORMAL, D2D1::RectF(left, 23*s, right, 40*s), p.secondary, align, nullptr, alpha);
    Text(index == expanded_ ? L"" : L"", 10, DWRITE_FONT_WEIGHT_NORMAL,
         D2D1::RectF(model_.arabic ? 8*s : w-26*s, 0, model_.arabic ? 26*s : w-8*s, h), p.secondary, DWRITE_TEXT_ALIGNMENT_CENTER, L"Segoe MDL2 Assets", alpha);
  } else {
    // Open Sukut: the accent action, like the phone's filled buttons.
    brush_->SetColor(Color(p.accent, alpha)); target_->FillRoundedRectangle(shape, brush_);
    if (hovered) { brush_->SetColor(Color(p.on_accent, pressed ? .16F : .08F)); target_->FillRoundedRectangle(shape, brush_); }
    const float icon_x=model_.arabic?w/2+48*s:w/2-68*s;
    Text(L"",14,DWRITE_FONT_WEIGHT_NORMAL,D2D1::RectF(icon_x,0,icon_x+18*s,h),p.on_accent,DWRITE_TEXT_ALIGNMENT_CENTER,L"Segoe MDL2 Assets",alpha);
    Text(model_.arabic ? L"فتح سُكوت" : L"Open Sukut", 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, D2D1::RectF(0,0,w,h), p.on_accent, DWRITE_TEXT_ALIGNMENT_CENTER, nullptr, alpha);
  }
  if (focus) { brush_->SetColor(Color(p.ink)); target_->DrawRoundedRectangle(shape, brush_, 2*s); }
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) { ReleaseResources(); CreateResources(); }
}

void TrayPopup::DrawListItem(const DRAWITEMSTRUCT& item) {
  const auto& choices = CurrentChoices();
  if (item.itemID >= choices.size() || !target_ || FAILED(target_->BindDC(item.hDC, &item.rcItem))) return;
  const auto p = PaletteFor(model_.dark);
  const float s = Scale(), full_width = static_cast<float>(item.rcItem.right-item.rcItem.left), height = static_cast<float>(item.rcItem.bottom-item.rcItem.top);
  const float rail=ScrollGeometry().Visible()?static_cast<float>(Px(12)):0, width=full_width-rail, origin=model_.arabic?rail:0;
  const bool selected = (item.itemState & ODS_SELECTED) != 0;
  const bool hover=static_cast<int>(item.itemID)==list_hover_;
  target_->BeginDraw(); target_->Clear(Color(selected ? p.list_selected : hover ? p.list_hover : p.list));
  const float icon_x=origin+(model_.arabic?width-30*s:10*s);
  const auto choice_color=expanded_==7?BufferColor(static_cast<int>(item.itemID),model_.dark):selected?accent_:p.secondary;
  Text(expanded_==7?BufferGlyph(static_cast<int>(item.itemID)):expanded_==5?OutputGlyph(choices[item.itemID].name):choices[item.itemID].id=="0"?L"":L"",15,DWRITE_FONT_WEIGHT_NORMAL,D2D1::RectF(icon_x,0,icon_x+20*s,height),choice_color,DWRITE_TEXT_ALIGNMENT_CENTER,L"Segoe MDL2 Assets");
  Text(choices[item.itemID].name, 12.5F, selected ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
       D2D1::RectF(origin+(model_.arabic?12:38)*s, 0, origin+width-(model_.arabic?38:12)*s, height), selected ? accent_ : p.ink,
       model_.arabic ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING);
  if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
    brush_->SetColor(Color(accent_)); target_->DrawRectangle(D2D1::RectF(origin+s,s,origin+width-s,height-s), brush_, s);
  }
  if (target_->EndDraw() == D2DERR_RECREATE_TARGET) { ReleaseResources(); CreateResources(); }
}
TrayScroll TrayPopup::ScrollGeometry() const {
  RECT box{};GetClientRect(list_,&box);
  return TrayScroll::Calculate(static_cast<int>(SendMessageW(list_,LB_GETCOUNT,0,0)),box.bottom,
      static_cast<int>(SendMessageW(list_,LB_GETITEMHEIGHT,0,0)),Px(24),static_cast<int>(SendMessageW(list_,LB_GETTOPINDEX,0,0)),Px(4));
}
bool TrayPopup::OnScrollRail(int x) const {
  if(!ScrollGeometry().Visible())return false;
  RECT box{};GetClientRect(list_,&box);
  return model_.arabic?x>=0 && x<Px(12):x>=box.right-Px(12) && x<box.right;
}
void TrayPopup::ScrollList(int top) {
  const auto geometry=ScrollGeometry();
  top=std::clamp(top,0,geometry.maximum);
  if(top==SendMessageW(list_,LB_GETTOPINDEX,0,0))return;
  list_hover_=-1;SendMessageW(list_,LB_SETTOPINDEX,top,0);
  InvalidateRect(list_,nullptr,FALSE);
}
void TrayPopup::PaintScrollBar(HDC supplied) {
  const auto geometry=ScrollGeometry();
  if(!geometry.Visible() || !target_)return;
  RECT rail{};GetClientRect(list_,&rail);
  if(model_.arabic)rail.right=Px(12);else rail.left=rail.right-Px(12);
  HDC dc=supplied?supplied:GetDC(list_);
  if(dc && SUCCEEDED(target_->BindDC(dc,&rail))) {
    const float width=static_cast<float>(rail.right-rail.left),thumb_width=static_cast<float>(Px(scroll_hover_||scroll_dragging_?6:4));
    const float top=static_cast<float>(Px(4)+geometry.offset);
    target_->BeginDraw();target_->Clear(Color(PaletteFor(model_.dark).list));
    brush_->SetColor(Color(model_.dark?(scroll_hover_||scroll_dragging_?0xB0B0B0:0x858585):(scroll_hover_||scroll_dragging_?0x666666:0x999999)));
    target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF((width-thumb_width)/2,top,(width+thumb_width)/2,top+static_cast<float>(geometry.thumb)),thumb_width/2,thumb_width/2),brush_);
    if(target_->EndDraw()==D2DERR_RECREATE_TARGET){ReleaseResources();CreateResources();}
  }
  if(dc && !supplied)ReleaseDC(list_,dc);
}
void TrayPopup::PaintSlider(HDC dc) {
  RECT box{},channel{},thumb{};GetClientRect(background_slider_,&box);
  SendMessageW(background_slider_,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));
  SendMessageW(background_slider_,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
  if(!target_ || FAILED(target_->BindDC(dc,&box)))return;
  const auto p=PaletteFor(model_.dark);const float s=Scale(),alpha=IsWindowEnabled(background_slider_)?1.0F:.45F;
  const float left=static_cast<float>(channel.left),right=static_cast<float>(channel.right),y=(box.bottom-box.top)/2.0F;
  float x=(thumb.left+thumb.right)/2.0F;
  // The native RTL trackbar's DC already mirrors the logical channel/shaft.
  // Drawing another mirror here reverses the fill and misplaces its thumb.
  const float start=left;
  target_->BeginDraw();target_->Clear(Color(p.background));
  brush_->SetColor(Color(p.secondary,.30F*alpha));
  target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(left,y-2*s,right,y+2*s),2*s,2*s),brush_);
  brush_->SetColor(Color(p.accent,alpha));
  target_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(std::min(start,x),y-2*s,std::max(start,x),y+2*s),2*s,2*s),brush_);
  if(background_hover_ || background_tracking_ || GetFocus()==background_slider_) {
    brush_->SetColor(Color(p.accent,.14F*alpha));target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),13*s,13*s),brush_);
  }
  brush_->SetColor(Color(p.card,alpha));target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),9*s,9*s),brush_);
  brush_->SetColor(Color(p.accent,alpha));target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x,y),6*s,6*s),brush_);
  if(GetFocus()==background_slider_) {
    brush_->SetColor(Color(p.accent,.8F*alpha));
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(s,s,box.right-s,box.bottom-s),6*s,6*s),brush_,s);
  }
  if(target_->EndDraw()==D2DERR_RECREATE_TARGET){ReleaseResources();CreateResources();}
}
LRESULT CALLBACK TrayPopup::SliderProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
  auto* self=reinterpret_cast<TrayPopup*>(data);
  if(msg==WM_ERASEBKGND)return 1;
  if(msg==WM_PAINT) {
    PAINTSTRUCT paint{};HDC dc=w?reinterpret_cast<HDC>(w):BeginPaint(hwnd,&paint);
    self->PaintSlider(dc);if(!w)EndPaint(hwnd,&paint);return 0;
  }
  if(msg==WM_PRINTCLIENT){self->PaintSlider(reinterpret_cast<HDC>(w));return 0;}
  if(msg==WM_MOUSEMOVE && !self->background_hover_) {
    self->background_hover_=true;TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,hwnd,0};TrackMouseEvent(&tracking);InvalidateRect(hwnd,nullptr,FALSE);
  }
  if(msg==WM_MOUSELEAVE){self->background_hover_=false;InvalidateRect(hwnd,nullptr,FALSE);}
  if(msg==WM_LBUTTONDOWN && !self->model_.busy)self->background_tracking_=true;
  if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS)InvalidateRect(hwnd,nullptr,FALSE);
  return DefSubclassProc(hwnd,msg,w,l);
}
LRESULT CALLBACK TrayPopup::ButtonProcedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
  auto* self = reinterpret_cast<TrayPopup*>(data);
  if (msg == WM_MOUSEMOVE && self->hover_ != static_cast<int>(id)) {
    const int previous = self->hover_; self->hover_ = static_cast<int>(id);
    TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
    if (previous >= 0) InvalidateRect(self->buttons_[previous], nullptr, FALSE);
    InvalidateRect(hwnd, nullptr, FALSE);
  } else if (msg == WM_MOUSELEAVE && self->hover_ == static_cast<int>(id)) { self->hover_ = -1; InvalidateRect(hwnd, nullptr, FALSE); }
  else if (msg == WM_ERASEBKGND) return 1;
  return DefSubclassProc(hwnd, msg, w, l);
}
LRESULT CALLBACK TrayPopup::ListProcedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
  auto* self=reinterpret_cast<TrayPopup*>(data);
  if(msg==WM_LBUTTONDOWN && self->OnScrollRail(GET_X_LPARAM(l))) {
    if(self->model_.busy)return 0;
    SetFocus(hwnd);const auto geometry=self->ScrollGeometry();const int y=GET_Y_LPARAM(l)-self->Px(4);
    if(y>=geometry.offset && y<geometry.offset+geometry.thumb) {
      self->scroll_dragging_=true;self->scroll_grab_=y-geometry.offset;SetCapture(hwnd);
    } else self->ScrollList(static_cast<int>(SendMessageW(hwnd,LB_GETTOPINDEX,0,0))+(y<geometry.offset?-geometry.rows:geometry.rows));
    self->PaintScrollBar();return 0;
  }
  if(msg==WM_MOUSEMOVE && self->scroll_dragging_) {
    self->ScrollList(self->ScrollGeometry().TopFromOffset(GET_Y_LPARAM(l)-self->Px(4)-self->scroll_grab_));return 0;
  }
  if(msg==WM_LBUTTONUP && self->scroll_dragging_) {
    self->scroll_dragging_=false;ReleaseCapture();self->PaintScrollBar();return 0;
  }
  if(msg==WM_CAPTURECHANGED){self->scroll_dragging_=false;self->PaintScrollBar();}
  if(msg==WM_MOUSEWHEEL) {
    if(self->model_.busy)return 0;
    self->wheel_remainder_+=GET_WHEEL_DELTA_WPARAM(w);
    const int steps=self->wheel_remainder_/WHEEL_DELTA;self->wheel_remainder_%=WHEEL_DELTA;
    UINT lines=3;SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
    const int rows=lines==WHEEL_PAGESCROLL?self->ScrollGeometry().rows:static_cast<int>(std::min(lines,100U));
    self->ScrollList(static_cast<int>(SendMessageW(hwnd,LB_GETTOPINDEX,0,0))-steps*rows);return 0;
  }
  if(msg==WM_MOUSEMOVE || msg==WM_MOUSELEAVE) {
    const auto item=SendMessageW(hwnd,LB_ITEMFROMPOINT,0,l);
    const bool rail=msg==WM_MOUSEMOVE && self->OnScrollRail(GET_X_LPARAM(l));
    const int next=msg==WM_MOUSELEAVE || rail || HIWORD(item)?-1:LOWORD(item);
    if(next!=self->list_hover_) {
      for(int row:{self->list_hover_,next})if(row>=0){RECT bounds{};if(SendMessageW(hwnd,LB_GETITEMRECT,row,reinterpret_cast<LPARAM>(&bounds))!=LB_ERR)InvalidateRect(hwnd,&bounds,FALSE);}
      self->list_hover_=next;
    }
    if(rail!=self->scroll_hover_){self->scroll_hover_=rail;self->PaintScrollBar();}
    if(msg==WM_MOUSEMOVE) { TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,hwnd,0}; TrackMouseEvent(&track); }
  }
  const auto result=DefSubclassProc(hwnd,msg,w,l);
  if(msg==WM_PAINT || msg==WM_PRINTCLIENT)self->PaintScrollBar(msg==WM_PRINTCLIENT?reinterpret_cast<HDC>(w):nullptr);
  return result;
}
LRESULT CALLBACK TrayPopup::Procedure(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
  auto* self = reinterpret_cast<TrayPopup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  return self && self->hwnd_ == hwnd ? self->Handle(msg,w,l) : DefWindowProcW(hwnd,msg,w,l);
}
LRESULT TrayPopup::Handle(UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_PAINT: Paint(); return 0;
    case WM_PRINTCLIENT: PaintToDc(reinterpret_cast<HDC>(w)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC:
      SetTextColor(reinterpret_cast<HDC>(w),model_.dark?RGB(203,203,203):RGB(93,93,93));
      SetBkMode(reinterpret_cast<HDC>(w),TRANSPARENT);
      return reinterpret_cast<LRESULT>(background_brush_);
    case WM_CTLCOLORLISTBOX: return reinterpret_cast<LRESULT>(background_brush_);
    case WM_DRAWITEM: {
      const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(l);
      if (item.CtlID == kListId) DrawListItem(item); else DrawButton(item);
      return TRUE;
    }
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = Px(36); return TRUE;
    case WM_HSCROLL:
      if(reinterpret_cast<HWND>(l)==background_slider_ && background_slider_ && !model_.busy) {
        background_preview_=std::clamp(static_cast<int>(SendMessageW(background_slider_,TBM_GETPOS,0,0)),-20,10);
        const bool commit=LOWORD(w)==TB_ENDTRACK;
        background_tracking_=!commit;
        InvalidateRect(background_slider_,nullptr,FALSE);InvalidateRect(hwnd_,nullptr,FALSE);
        if(commit && background_preview_!=model_.optimized_background_db) {
          model_.optimized_background_db=background_preview_;
          if(actions_.set_background)actions_.set_background(background_preview_);
        }
      }
      return 0;
    case WM_ACTIVATE: if (LOWORD(w) == WA_INACTIVE) PostMessageW(hwnd_, WM_CLOSE,0,0); return 0;
    case WM_CLOSE: Close(); return 0;
    case WM_DPICHANGED:
      dpi_ = HIWORD(w);
      ReleaseResources(); CreateResources();
      SendMessageW(list_, LB_SETITEMHEIGHT,0,Px(36)); Layout(); Position();
      RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN); return 0;
    case WM_COMMAND: {
      const int id = LOWORD(w), index = id-kFirstId;
      if (id == IDCANCEL) { if (expanded_ >= 0) { const int old = expanded_; Expand(old); SetFocus(buttons_[old]); } else Close(); return 0; }
      if (id == kListId && HIWORD(w) == LBN_SELCHANGE && !model_.busy) {
        const int choice = static_cast<int>(SendMessageW(list_, LB_GETCURSEL,0,0));
        const auto& choices = CurrentChoices();
        if (choice >= 0 && choice < static_cast<int>(choices.size())) {
          const auto value = choices[choice].id; const int section = expanded_;
          Expand(section); SetFocus(buttons_[section]);
          if (section == 5 && actions_.set_output) actions_.set_output(value);
          if (section == 6 && actions_.set_source) actions_.set_source(value);
          if (section == 7 && actions_.set_buffering) actions_.set_buffering(std::atoi(value.c_str()));
        }
      } else if (index == 0 && !model_.busy && actions_.select_mode) actions_.select_mode(model_.mode == 0 ? SelectedTile(model_) : 0);
      else if (index >= 1 && index < 4 && !model_.busy && actions_.select_mode) actions_.select_mode(index);
      else if (index == 4) { Close(); if (actions_.open_window) actions_.open_window(); }
      else if ((index >= 5 && index <= 7) && !model_.busy) Expand(index);
      return 0;
    }
    default: break;
  }
  return DefWindowProcW(hwnd_,msg,w,l);
}
}  // namespace sukut::host
