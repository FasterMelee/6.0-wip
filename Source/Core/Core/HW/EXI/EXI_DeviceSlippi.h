// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <SlippiGame.h>
#include <string>
#include <unordered_map>
#include <deque>
#include <ctime>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Common/File.h"
#include "Core/Slippi/SlippiReplayComm.h"

namespace ExpansionInterface
{
// Slippi device emulation for Melee
class CEXISlippi final : public IEXIDevice
{
public:
  CEXISlippi();
  ~CEXISlippi();

  void DMAWrite(u32 address, u32 size) override;
  void DMARead(u32 address, u32 size) override;

  bool IsPresent() const override;

private:
  enum
  {
    CMD_UNKNOWN = 0x0,
    CMD_RECEIVE_COMMANDS = 0x35,
    CMD_RECEIVE_GAME_INFO = 0x36,
    CMD_RECEIVE_POST_FRAME_UPDATE = 0x38,
    CMD_RECEIVE_GAME_END = 0x39,
    CMD_PREPARE_REPLAY = 0x75,
    CMD_READ_FRAME = 0x76,
    CMD_GET_LOCATION = 0x77,
    CMD_IS_FILE_READY = 0x88,
    CMD_IS_STOCK_STEAL = 0x89,
  };

  std::unordered_map<u8, u32> payloadSizes = {
      // The actual size of this command will be sent in one byte
      // after the command is received. The other receive command IDs
      // and sizes will be received immediately following
      {CMD_RECEIVE_COMMANDS, 1},

      // The following are all commands used to play back a replay and
      // have fixed sizes
      {CMD_PREPARE_REPLAY, 0},
      {CMD_READ_FRAME, 4},
      {CMD_IS_STOCK_STEAL, 5},
      {CMD_GET_LOCATION, 6},
      {CMD_IS_FILE_READY, 0}};

  // Communication with Launcher
  SlippiReplayComm* replayComm;

  // .slp File creation stuff
  u32 writtenByteCount = 0;

  // vars for metadata generation
  time_t gameStartTime;
  int32_t lastFrame;
  std::unordered_map<u8, std::unordered_map<u8, u32>> characterUsage;

  void updateMetadataFields(u8* payload, u32 length);
  void configureCommands(u8* payload, u8 length);
  void writeToFile(u8* payload, u32 length, std::string fileOption);
  std::vector<u8> generateMetadata();
  void createNewFile();
  void closeFile();
  std::string generateFileName();
  bool checkFrameFullyFetched(int32_t frameIndex);

  // std::ofstream log;

  File::IOFile m_file;
  std::vector<u8> m_payload;

  // replay playback stuff
  void loadFile(std::string path);
  void prepareGameInfo();
  void prepareCharacterFrameData(int32_t frameIndex, u8 port, u8 isFollower);
  void prepareFrameData(u8* payload);
  void prepareIsStockSteal(u8* payload);
  void prepareIsFileReady();

  std::unordered_map<u8, std::string> getNetplayNames();

  std::vector<u8> m_read_queue;
  Slippi::SlippiGame* m_current_game = nullptr;

  void TransferByte(u8& byte) override;
};
}  // namespace ExpansionInterface
