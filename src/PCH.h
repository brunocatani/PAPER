#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define NOMMNOSOUND

#include "F4SE/F4SE.h"
#include "RE/Fallout.h"
#include "REL/Relocation.h"

#include <algorithm>
#include <array>

using namespace std::literals;

#include "support/Logger.h"
#include "Version.h"

#define DLLEXPORT __declspec(dllexport)
