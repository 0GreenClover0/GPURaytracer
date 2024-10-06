#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers.
#endif

#include <windows.h>

// C RunTime Header Files
#include <iomanip>
#include <sstream>
#include <stdlib.h>

#include <assert.h>
#include <list>
#include <memory>
#include <shellapi.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

#include "d3dx12.h"
#include <d3d12.h>
#include <dxgi1_6.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "DeviceResources.h"
#include "RendererRaytracingHelper.h"
