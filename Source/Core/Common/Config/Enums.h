// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <array>

namespace Config
{
enum class LayerType
{
  Base,
  GlobalGame,
  LocalGame,
  Movie,
  Netplay,
  CommandLine,
  CurrentRun,
  Meta,
  FMOverride
};

enum class System
{
  Main,
  SYSCONF,
  GCPad,
  WiiPad,
  GCKeyboard,
  GFX,
  Logger,
  Debugger,
  FM
};

constexpr std::array<LayerType, 8> SEARCH_ORDER{{
    LayerType::FMOverride,
    LayerType::CurrentRun,
    LayerType::CommandLine,
    LayerType::Movie,
    LayerType::Netplay,
    LayerType::LocalGame,
    LayerType::GlobalGame,
    LayerType::Base,
}};
}
