#pragma once

#include "Types.h"
#include <CPP/Windows/PropVariant.h>
#include <windows.h>

namespace GFArch
{

NWindows::NCOM::CPropVariant GetProperty(PROPID propId);

}
