// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Common/Version.h"

#include <string>

#include "Common/scmrev.h"

namespace Common
{
const std::string git_commit = GIT_COMMIT;
const std::string git_version = GIT_VERSION;

#ifdef _WIN32
const std::string netplay_version = GIT_VERSION " Windows";
#elif __APPLE__
const std::string netplay_version = GIT_VERSION " Mac";
#else
const std::string netplay_version = GIT_VERSION " Linux";
#endif
}  // namespace Common
