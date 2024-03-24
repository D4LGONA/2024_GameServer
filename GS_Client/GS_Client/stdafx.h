#pragma once
#pragma comment(linker,"/entry:WinMainCRTStartup /subsystem:console")
#include <iostream>
#include <WS2tcpip.h>
#include <Windows.h>
#include <tchar.h>
#include <random>
#include <vector>
#include <atlimage.h>
#include <fstream>
#include <queue>
#include <cmath>
#include <string>
#include <array>

#pragma comment (lib, "WS2_32.LIB") // 옛날옛날시절 라이브러리에서 시작한것.. 그래서 32임


#define WIDTHMAX 660
#define HEIGHTMAX 685
#define MAGENTA RGB(255,0,255)

#include "Image.h"
