#include "window/TrayPlacement.h"
#include <cassert>
#include <iostream>
int main(){using sukut::window::TrayLeft;
 assert(TrayLeft(960,1920,340)==790);assert(TrayLeft(1200,1920,340)==1030);
 assert(TrayLeft(1880,1920,340)==1572);assert(TrayLeft(24,1920,340)==8);
 assert(TrayLeft(960,1920,420)==750);assert(TrayLeft(1270,1280,340)==932);
 std::cout<<"PASS: tray-icon centring, screen-edge clamping, work-area width and wider menu\n";}
