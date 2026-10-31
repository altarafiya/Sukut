#include "sukut-host/DefaultOutputChanges.h"
#include "sukut-host/ModeAppearance.h"
#include <iostream>
#include <stdexcept>
using namespace sukut::host;
void Check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
void CheckModeIcons() {
  BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),3,-1,1,32,BI_RGB,0,0,0,0,0};
  HDC dc=GetDC(nullptr);void* bits=nullptr;
  HBITMAP color=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
  Check(color && bits,"test bitmap creation failed");
  DWORD sourcePixels[]{0,0x80808080,0xFFFFFFFF};memcpy(bits,sourcePixels,sizeof(sourcePixels));
  HBITMAP mask=CreateBitmap(3,1,1,1,nullptr);ICONINFO original{TRUE,0,0,mask,color};
  HICON source=CreateIconIndirect(&original);DeleteObject(color);DeleteObject(mask);
  Check(source!=nullptr,"test icon creation failed");
  const DWORD beforeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  const DWORD beforeUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
  for(int n=0;n<120;++n) {
    int mode=n%3+1;const DWORD rgb=ModeColor(mode,true);HICON tinted=TintIcon(source,rgb);
    Check(tinted!=nullptr,"mode icon tint failed");ICONINFO icon{};Check(GetIconInfo(tinted,&icon),"tinted icon unavailable");
    DWORD pixels[3]{};Check(GetDIBits(dc,icon.hbmColor,0,1,pixels,&info,DIB_RGB_COLORS)==1,"tinted icon pixels unavailable");
    Check(pixels[0]==0 && pixels[2]==(0xFF000000|rgb),"transparent or opaque tint is incorrect");
    Check((pixels[1]>>24)==128 && ((pixels[1]>>16)&255)==((((rgb>>16)&255)*128+127)/255),"partial alpha coverage changed");
    DeleteObject(icon.hbmColor);DeleteObject(icon.hbmMask);DestroyIcon(tinted);
  }
  Check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)==beforeGdi,"mode icon leaks GDI objects");
  Check(GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS)==beforeUser,"mode icon leaks USER objects");
  DestroyIcon(source);ReleaseDC(nullptr,dc);
  std::cout<<"PASS mode icon colours, alpha coverage and 120 replacements without handle leaks\n";
}
int main() {
  try {
    CheckModeIcons();
    const std::vector<EngineStatus::Endpoint> before{{"speakers","Speakers",1,false},{"cable","VB-Audio",1,true}};
    auto after = before; after.push_back({"headphones","Headphones",1,false});
    DefaultOutputChanges changes;
    changes.Observe("headphones",false,"cable");
    Check(!changes.Resolve(before) && changes.Pending(),"notification before enumeration was lost");
    changes.Observe("cable",false,"cable");
    changes.Observe("speakers",true,"cable");
    Check(changes.Resolve(after) == "headphones","Sukut's own routing notification replaced Windows' choice");
    Check(!changes.Resolve(after),"one notification applied twice");
    changes.Observe("headphones",false,"cable"); changes.Observe("speakers",false,"cable");
    Check(changes.Resolve(after) == "speakers","rapid changes did not follow the latest Windows choice");
    changes.Observe("other-virtual",false,"cable");
    auto virtuals = after; virtuals.push_back({"other-virtual","Another virtual output",1,true});
    Check(!changes.Resolve(virtuals) && !changes.Pending(),"virtual output accepted");
    changes.Observe("capture",false,"cable");
    auto capture = after; capture.push_back({"capture","Microphone",0,false});
    Check(!changes.Resolve(capture) && !changes.Pending(),"capture endpoint accepted");
    changes.Observe("removed",false,"cable");
    for(int i=0;i<40;++i) Check(!changes.Resolve(before),"missing device accepted");
    Check(!changes.Pending(),"missing endpoint retried forever");
    changes.Observe("headphones",false,"cable");
    Check(changes.Resolve(after) == "headphones","same selected output must still repair externally changed routing");
    changes.ObserveArrival("headphones", "cable");
    changes.Observe("speakers",true,"cable");
    changes.ObserveArrival("capture", "cable");
    changes.ObserveArrival("other-virtual", "cable");
    auto burst = after;
    burst.push_back({"capture","Bluetooth microphone",0,false});
    burst.push_back({"other-virtual","Virtual cable",1,true});
    Check(changes.Resolve(burst)=="headphones","capture/virtual arrival or internal command hid a physical connection");
    Check(!changes.Pending(),"resolved arrival replayed an older candidate");
    changes.ObserveArrival("headphones", "cable");
    for(int i=0;i<12;++i) Check(!changes.Resolve(before) && changes.Pending(),"slow Bluetooth enumeration expired too early");
    Check(changes.Resolve(after)=="headphones","late Bluetooth endpoint was lost");
    changes.ObserveArrival("other-virtual", "cable");
    Check(!changes.Resolve(burst) && !changes.Pending(),"virtual connection accepted");
    changes.ObserveArrival("gone", "cable");
    for(int i=0;i<40;++i) Check(!changes.Resolve(before),"missing arrival accepted");
    Check(!changes.Pending(),"arrival discovery never stopped retrying");
    changes.ObserveArrival("headphones", "cable");
    changes.Observe("speakers",false,"cable");
    Check(changes.Resolve(after)=="speakers","a subsequent Windows default choice was overridden by arrival");
    std::cout << "PASS delayed enumeration, own-cable suppression, coalescing, physical-render validation, bounded retry, repeated external choice\n";
    std::cout << "PASS connection during command, capture/virtual arrival burst, slow Bluetooth readiness, bounded arrival retry and later Windows choice\n";
    return 0;
  } catch(const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
