// Copyright 2018 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <string>

#include "Common/Config/Config.h"

#define FM_GAMETYPE_NONE 0
#define FM_GAMETYPE_SSBM (1 << 0)
#define FM_GAMETYPE_SSBM_20XX (1 << 1)

namespace Config
{
  using GameType = u8;

  // this is set automatically depending on the underlying GameConfigLoader
  extern const ConfigInfo<GameType> FM_GAME_TYPE;
}
