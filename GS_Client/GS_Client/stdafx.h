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

#pragma comment (lib, "WS2_32.LIB") // ������������ ���̺귯������ �����Ѱ�.. �׷��� 32��


#define WIDTHMAX 660
#define HEIGHTMAX 685
#define MAGENTA RGB(255,0,255)

#include "Image.h"
