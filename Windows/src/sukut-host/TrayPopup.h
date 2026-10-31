#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <functional>
#include <string>
#include <vector>
#include "sukut-host/TrayScroll.h"

namespace sukut::host {

// Native flyout in the phone layout: title/status with a round power button, three mode
// tiles, playback rows (buffering, output, source app) and Open Sukut. Built on demand and destroyed on
// close (window, Direct2D and DirectWrite objects included), so nothing of it
// stays in memory while Sukut runs in the tray. Real BUTTON children keep
// keyboard navigation and screen-reader names; Direct2D only paints them.
class TrayPopup {
 public:
  struct Choice {
    std::string id;
    std::wstring name;
    bool operator==(const Choice&) const = default;
  };
  struct Model {
    bool arabic = false, dark = false, busy = false;
    int mode = 0;              // 0 Off, 1 Music Filter, 2 Optimized, 3 Strict
    int preferred = 1;         // Mode the power button turns on (last used, 1-3)
    std::wstring status;       // e.g. "Active · Music Filter"
    bool attention = false, standby = false;
    int buffering = 0;
    int optimized_background_db = 0;
    std::string output, source_pid = "0";
    std::vector<Choice> outputs, apps;
    bool operator==(const Model&) const = default;
  };
  struct Actions {
    std::function<void(int)> select_mode;
    std::function<void()> open_window;
    std::function<void(int)> set_buffering;
    std::function<void(const std::string&)> set_output, set_source;
    std::function<void()> closed;  // after the flyout and its graphics are released
    std::function<void()> refresh_apps;  // explicit expansion, no idle scan
    std::function<void(int)> set_background;  // committed on release/key-up only
  };

  TrayPopup(HINSTANCE instance, Actions actions);
  ~TrayPopup();
  TrayPopup(const TrayPopup&) = delete;
  TrayPopup& operator=(const TrayPopup&) = delete;

  void Show(const RECT& anchor, const Model& model);
  void Update(Model model);
  void Close();
  [[nodiscard]] bool IsOpen() const { return hwnd_ != nullptr; }
  [[nodiscard]] HWND Window() const { return hwnd_; }
  // Time the popup last closed, to treat a click on the tray icon that
  // dismissed it as "close", not "open again".
  [[nodiscard]] ULONGLONG ClosedAt() const { return closed_at_; }

 private:
  friend struct TrayPopupTests;
  static LRESULT CALLBACK Procedure(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK ButtonProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
  static LRESULT CALLBACK ListProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
  static LRESULT CALLBACK SliderProcedure(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
  LRESULT Handle(UINT, WPARAM, LPARAM);
  bool InitializeWindow();
  void CreateResources();
  void ReleaseResources();
  void Layout();
  void Paint();
  void PaintToDc(HDC dc);
  void PaintSlider(HDC dc);
  static const wchar_t* BufferGlyph(int level);
  static UINT32 BufferColor(int level, bool dark);
  void DrawButton(const DRAWITEMSTRUCT& item);
  void ApplyTexts();
  void Position(bool animate = false);
  void Expand(int section);
  void PopulateList();
  void DrawListItem(const DRAWITEMSTRUCT& item);
  [[nodiscard]] TrayScroll ScrollGeometry() const;
  [[nodiscard]] bool OnScrollRail(int x) const;
  void PaintScrollBar(HDC dc = nullptr);
  void ScrollList(int top);
  const std::vector<Choice>& CurrentChoices() const;
  [[nodiscard]] float Scale() const { return dpi_ / 96.0F; }
  [[nodiscard]] int Px(float dips) const { return static_cast<int>(dips * Scale() + 0.5F); }
  [[nodiscard]] D2D1_COLOR_F Color(UINT32 rgb, float alpha = 1.0F) const;
  [[nodiscard]] UINT32 Accent() const;
  void Text(const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, const D2D1_RECT_F& box, UINT32 color,
            DWRITE_TEXT_ALIGNMENT align, const wchar_t* family = nullptr, float alpha = 1.0F);

  HINSTANCE instance_;
  Actions actions_;
  Model model_;
  HWND hwnd_ = nullptr;
  HWND buttons_[8]{}, list_ = nullptr;
  HWND background_slider_ = nullptr;
  bool background_tracking_ = false, background_hover_ = false;
  int background_preview_ = 0;
  float background_top_ = 0;
  std::vector<Choice> buffering_choices_;
  RECT anchor_{};
  int expanded_ = -1;
  float height_ = 414, expansion_height_ = 0, playback_top_ = 216, modes_top_ = 108;
  int final_x_ = 0, final_y_ = 0;
  HBRUSH background_brush_ = nullptr;
  int hover_ = -1;
  int list_hover_ = -1;
  bool scroll_hover_ = false, scroll_dragging_ = false;
  int scroll_grab_ = 0, wheel_remainder_ = 0;
  UINT dpi_ = 96;
  ULONGLONG closed_at_ = 0;
  UINT32 accent_ = 0, accent_text_ = 0;
  ID2D1Factory* d2d_ = nullptr;
  ID2D1DCRenderTarget* target_ = nullptr;
  ID2D1SolidColorBrush* brush_ = nullptr;
  IDWriteFactory* dwrite_ = nullptr;
  struct TextFormat {
    float size; DWRITE_FONT_WEIGHT weight; DWRITE_TEXT_ALIGNMENT align; bool arabic;
    std::wstring family; IDWriteTextFormat* format; IDWriteInlineObject* ellipsis;
  };
  std::vector<TextFormat> formats_;
};

}  // namespace sukut::host
