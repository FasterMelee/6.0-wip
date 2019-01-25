// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <vector>
#include <QDialog>

#include "Core/NetPlayClient.h"
#include "Core/NetPlayServer.h"
#include "VideoCommon/OnScreenDisplay.h"

#include "Common/Config/Config.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/NetplaySettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/ConfigManager.h"

#include "UICommon/GameFile.h"

class MD5Dialog;
class GameListModel;
class PadMappingDialog;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QDoubleSpinBox;
class QSplitter;
class QTableWidget;
class QTextEdit;
class QToolButton;

class NetPlayDialog : public QDialog, public NetPlay::NetPlayUI
{
  Q_OBJECT
public:
  explicit NetPlayDialog(QWidget* parent = nullptr);
  ~NetPlayDialog();

  void show(std::string nickname, bool use_traversal);
  void reject() override;

  // NetPlayUI methods
  void BootGame(const std::string& filename) override;
  void StopGame() override;
  bool IsHosting() const override;

  void Update() override;
  void AppendChat(const std::string& msg) override;

  void OnMsgChangeGame(const std::string& filename) override;
  void OnMsgStartGame() override;
  void OnMsgStopGame() override;
  void OnMinimumPadBufferChanged(u32 buffer) override;
  void OnLocalPadBufferChanged(u32 buffer) override;
  void OnHostInputAuthorityChanged(bool enabled) override;
  void OnDesync(u32 frame, const std::string& player) override;
  void OnConnectionLost() override;
  void OnConnectionError(const std::string& message) override;
  void OnTraversalError(TraversalClient::FailureReason error) override;
  void OnTraversalStateChanged(TraversalClient::State state) override;
  void OnSaveDataSyncFailure() override;

  bool IsRecording() override;
  std::string FindGame(const std::string& game) override;
  std::shared_ptr<const UICommon::GameFile> FindGameFile(const std::string& game) override;
  void ShowMD5Dialog(const std::string& file_identifier) override;
  void SetMD5Progress(int pid, int progress) override;
  void SetMD5Result(int pid, const std::string& result) override;
  void AbortMD5() override;
signals:
  void Boot(const QString& filename);
  void Stop();

private:
  void CreateChatLayout();
  void CreatePlayersLayout();
  void CreateMainLayout();
  void ConnectWidgets();
  void OnChat();
  void OnStart();
  void DisplayMessage(const QString& msg, const std::string& color,
                      int duration = OSD::Duration::NORMAL);
  void UpdateDiscordPresence();
  void UpdateGUI();
  void GameStatusChanged(bool running);
  void SetOptionsEnabled(bool enabled);

  void SetGame(const QString& game_path);

  template<class T>
  T GetConfigOptionWithSelectedGame(const Config::ConfigInfo<T>& info)
  {
    const auto game = FindGameFile(m_current_game);

    // TODO: maybe not the right way to do it but it'll work for FM?
    if(game)
    {
      Config::AddLayer(
          ConfigLoaders::GenerateGlobalGameConfigLoader(game->GetGameID(), game->GetRevision(), game->GetLongName(DiscIO::Language::English)));
      Config::AddLayer(
          ConfigLoaders::GenerateLocalGameConfigLoader(game->GetGameID(), game->GetRevision(), game->GetLongName(DiscIO::Language::English)));
    }

    T value = Config::Get(info);

    if(game)
    {
      Config::RemoveLayer(Config::LayerType::GlobalGame);
      Config::RemoveLayer(Config::LayerType::LocalGame);
    }

    return value;
  }

  bool MeetsAutoBufferConditions();

  // Chat
  QGroupBox* m_chat_box;
  QTextEdit* m_chat_edit;
  QLineEdit* m_chat_type_edit;
  QPushButton* m_chat_send_button;

  // Players
  QGroupBox* m_players_box;
  QComboBox* m_room_box;
  QLabel* m_hostcode_label;
  QPushButton* m_hostcode_action_button;
  QLabel* m_longest_route_label;
  QTableWidget* m_players_list;
  QPushButton* m_kick_button;
  QPushButton* m_assign_ports_button;

  // Other
  QPushButton* m_game_button;
  QToolButton* m_md5_button;
  QPushButton* m_start_button;

  QLabel* m_minimum_buffer_label;
  QDoubleSpinBox* m_minimum_buffer_size_box;
  QPushButton* m_auto_buffer_button;

  QLabel* m_local_buffer_label;
  QDoubleSpinBox* m_local_buffer_size_box;
  QPushButton* m_local_under_minimum_warning;

  QCheckBox* m_save_sd_box;
  QCheckBox* m_load_wii_box;
  QCheckBox* m_sync_save_data_box;
  QCheckBox* m_record_input_box;
  QCheckBox* m_strict_settings_sync_box;
  QCheckBox* m_host_input_authority_box;
  QPushButton* m_quit_button;
  QSplitter* m_splitter;

  QGridLayout* m_main_layout;
  MD5Dialog* m_md5_dialog;
  PadMappingDialog* m_pad_mapping;
  std::string m_current_game;
  std::string m_external_ip_address;
  std::string m_nickname;
  GameListModel* m_game_list_model = nullptr;
  bool m_use_traversal = false;
  bool m_is_copy_button_retry = false;
  bool m_got_stop_request = true;
  int m_minimum_buffer_size = 0;
  int m_local_buffer_size = 0;
  int m_player_count = 0;
  int m_old_player_count = 0;
  bool m_host_input_authority = false;

  bool CalculateBufferFromSamples(const std::vector<NetPlay::NetPlayServer::NetRoute>& samples);
  void SampleAutoBuffer();

  QTimer* m_auto_buffer_sample_timer;
  std::vector<NetPlay::NetPlayServer::NetRoute> m_auto_buffer_samples;
  constexpr static int m_auto_buffer_sample_amount = 3;
};
