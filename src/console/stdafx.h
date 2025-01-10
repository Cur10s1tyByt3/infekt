/**
 * Copyright (C) 2010 syndicode
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 **/

#ifndef _STDAFX_H
#define _STDAFX_H

#ifdef _WIN32
#include "targetver.h"
#include <windows.h>
#include <shlwapi.h>
#include <process.h>
#include <shlobj.h>
#include <tchar.h>
#include <io.h>
#include <conio.h>
#define _tstring wstring
// going to get rid of tchar.h some day.
#endif

/* standard / system headers */
#include <limits>
#include <stdio.h>
#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <list>
#include <set>
#include <queue>
#include <stack>
#include <algorithm>
#include <iomanip>
#include <math.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <regex>
#include <functional>
#include <omp.h>

/* cairo and other lib headers */
#ifdef _WIN32
#include <cairo-win32.h>
#else
#include <cairo.h>
#endif

/* local headers */
#include "infekt.h"

#ifndef _WIN32
#include "infekt-posix.h"
#endif

#endif /* !_STDAFX_H */
