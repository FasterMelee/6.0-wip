// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/NetPlay/NetPlayDialog.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QTimer>

#include <sstream>

#include "Common/CommonPaths.h"
#include "Common/HttpRequest.h"
#include "Common/TraversalClient.h"

#include "Core/Core.h"

#include "DolphinQt/GameList/GameListModel.h"
#include "DolphinQt/NetPlay/GameListDialog.h"
#include "DolphinQt/NetPlay/MD5Dialog.h"
#include "DolphinQt/NetPlay/PadMappingDialog.h"
#include "DolphinQt/QtUtils/FlowLayout.h"
#include "DolphinQt/QtUtils/QueueOnObject.h"
#include "DolphinQt/QtUtils/RunOnObject.h"
#include "DolphinQt/Resources.h"
#include "DolphinQt/Settings.h"

#include "UICommon/DiscordPresence.h"

#include "VideoCommon/VideoConfig.h"

NetPlayDialog::NetPlayDialog(QWidget* parent)
    : QDialog(parent), m_game_list_model(Settings::Instance().GetGameListModel())
{
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  setWindowTitle(tr("NetPlay"));
  setWindowIcon(Resources::GetAppIcon());

  m_pad_mapping = new PadMappingDialog(this);
  m_md5_dialog = new MD5Dialog(this);

  CreateChatLayout();
  CreatePlayersLayout();
  CreateMainLayout();
  ConnectWidgets();

  auto& settings = Settings::Instance().GetQSettings();

  restoreGeometry(settings.value(QStringLiteral("netplaydialog/geometry")).toByteArray());
  m_splitter->restoreState(settings.value(QStringLiteral("netplaydialog/splitter")).toByteArray());
}

NetPlayDialog::~NetPlayDialog()
{
  auto& settings = Settings::Instance().GetQSettings();

  settings.setValue(QStringLiteral("netplaydialog/geometry"), saveGeometry());
  settings.setValue(QStringLiteral("netplaydialog/splitter"), m_splitter->saveState());
}

void NetPlayDialog::CreateMainLayout()
{
  m_main_layout = new QGridLayout;
  m_game_button = new QPushButton;
  m_md5_button = new QToolButton;
  m_start_button = new QPushButton(tr("Start"));
  m_minimum_buffer_size_box = new QDoubleSpinBox;
  m_auto_buffer_button = new QPushButton(tr("Auto"));
  m_local_buffer_size_box = new QDoubleSpinBox;
  m_local_under_minimum_warning = new QPushButton;
  m_save_sd_box = new QCheckBox(tr("Write save/SD data"));
  m_load_wii_box = new QCheckBox(tr("Load Wii Save"));
  m_sync_save_data_box = new QCheckBox(tr("Sync Saves"));
  m_record_input_box = new QCheckBox(tr("Record inputs"));
  m_strict_settings_sync_box = new QCheckBox(tr("Strict Settings Sync"));
  m_host_input_authority_box = new QCheckBox(tr("Host Input Authority"));
  m_minimum_buffer_label = new QLabel(tr("Minimum Buffer:"));
  m_local_buffer_label = new QLabel(tr("Buffer:"));
  m_quit_button = new QPushButton(tr("Quit"));
  m_splitter = new QSplitter(Qt::Horizontal);

  m_game_button->setDefault(false);
  m_game_button->setAutoDefault(false);

  m_sync_save_data_box->setChecked(true);

  if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) && !m_host_input_authority)
  {
    m_minimum_buffer_size_box->setDecimals(2); m_local_buffer_size_box->setDecimals(2);
    m_minimum_buffer_size_box->setSingleStep(0.25); m_local_buffer_size_box->setSingleStep(0.25);
    m_minimum_buffer_size_box->setSuffix(tr(" frame(s)")); m_local_buffer_size_box->setSuffix(tr(" frame(s)"));
  }
  else
  {
    m_minimum_buffer_size_box->setSingleStep(1); m_local_buffer_size_box->setSingleStep(1);
    m_minimum_buffer_size_box->setDecimals(0); m_local_buffer_size_box->setDecimals(0);
    m_minimum_buffer_size_box->setSuffix(QString::fromStdString("")); m_local_buffer_size_box->setSuffix(QString::fromStdString(""));
  }

  m_minimum_buffer_size_box->setRange(0, 10000); m_local_buffer_size_box->setRange(0, 10000);

  m_auto_buffer_button->setToolTip(tr(
    "Calculates buffer automatically based on the longest network route.\n"
    "Requires that:\n"
    "* The game being played has its polling method set to On SI Register Read\n"
    "* The game being played is an NTSC game (runs at 60hz)"));

  m_auto_buffer_sample_timer = new QTimer(this);
  m_auto_buffer_sample_timer->setInterval(1000);
  m_auto_buffer_sample_timer->setSingleShot(false);

  QIcon icon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
  m_local_under_minimum_warning->setIcon(icon);
  m_local_under_minimum_warning->setIconSize({ 16, 16 });

  // https://stackoverflow.com/questions/10794532/how-to-make-a-qt-widget-invisible-without-changing-the-position-of-the-other-qt
  QSizePolicy sp_retain = m_local_under_minimum_warning->sizePolicy();
  sp_retain.setRetainSizeWhenHidden(true);
  m_local_under_minimum_warning->setSizePolicy(sp_retain);
  m_local_under_minimum_warning->setHidden(true);
  m_local_under_minimum_warning->setToolTip(tr("Your local buffer is below the minimum buffer.\nThe minimum buffer value set by the host will be used."));
  m_local_under_minimum_warning->setFlat(true);

  connect(m_auto_buffer_sample_timer, &QTimer::timeout, this, &NetPlayDialog::SampleAutoBuffer);

  auto* default_button = new QAction(tr("Calculate MD5 hash"), m_md5_button);

  auto* menu = new QMenu(this);

  auto* other_game_button = new QAction(tr("Other game"), this);
  auto* sdcard_button = new QAction(tr("SD Card"), this);

  menu->addAction(other_game_button);
  menu->addAction(sdcard_button);

  connect(default_button, &QAction::triggered,
          [this] { Settings::Instance().GetNetPlayServer()->ComputeMD5(m_current_game); });
  connect(other_game_button, &QAction::triggered, [this] {
    GameListDialog gld(this);

    if (gld.exec() == QDialog::Accepted)
    {
      Settings::Instance().GetNetPlayServer()->ComputeMD5(gld.GetSelectedUniqueID().toStdString());
    }
  });
  connect(sdcard_button, &QAction::triggered,
          [] { Settings::Instance().GetNetPlayServer()->ComputeMD5(WII_SDCARD); });

  m_md5_button->setDefaultAction(default_button);
  m_md5_button->setPopupMode(QToolButton::MenuButtonPopup);
  m_md5_button->setMenu(menu);

  m_strict_settings_sync_box->setToolTip(
      tr("This will sync additional graphics settings, and force everyone to the same internal "
         "resolution.\nMay prevent desync in some games that use EFB reads. Please ensure everyone "
         "uses the same video backend."));
  m_host_input_authority_box->setToolTip(
      tr("This gives the host control over when inputs are sent to the game, effectively "
         "decoupling players from each other in terms of buffering.\nThis allows players to have "
         "latency based solely on their connection to the host, rather than everyone's connection. "
         "Buffer works differently\nin this mode. The host always has no latency, and the buffer "
         "setting serves to prevent stutter, speeding up when the amount of buffered\ninputs "
         "exceeds the set limit. Input delay is instead based on ping to the host. This results in "
         "smoother gameplay on unstable connections."));

  m_main_layout->addWidget(m_game_button, 0, 0);
  m_main_layout->addWidget(m_md5_button, 0, 1);
  m_main_layout->addWidget(m_splitter, 1, 0, 1, -1);

  m_splitter->addWidget(m_chat_box);
  m_splitter->addWidget(m_players_box);

  auto* options_widget = new QBoxLayout(QBoxLayout::TopToBottom);
  auto* top_widget = new QBoxLayout(QBoxLayout::LeftToRight);
  auto* minimum_buffer_widget = new QBoxLayout(QBoxLayout::LeftToRight);
  auto* local_buffer_widget = new QBoxLayout(QBoxLayout::LeftToRight);
  auto* bottom_widget = new FlowLayout;

  auto* minimum_buffer_box = new QBoxLayout(QBoxLayout::LeftToRight);
  minimum_buffer_box->addWidget(m_minimum_buffer_label);
  minimum_buffer_box->addWidget(m_minimum_buffer_size_box);
  minimum_buffer_widget->addLayout(minimum_buffer_box);
  minimum_buffer_widget->addWidget(m_auto_buffer_button);

  minimum_buffer_widget->setAlignment(Qt::AlignLeft);

  auto* local_buffer_box = new QBoxLayout(QBoxLayout::LeftToRight);
  local_buffer_box->addWidget(m_local_buffer_label);
  local_buffer_box->addWidget(m_local_buffer_size_box);
  local_buffer_widget->addLayout(local_buffer_box);
  local_buffer_widget->addWidget(m_local_under_minimum_warning, 0, Qt::AlignmentFlag::AlignVCenter);

  local_buffer_widget->setAlignment(Qt::AlignLeft);

  top_widget->addWidget(m_start_button, 0, Qt::AlignLeft);
  top_widget->addSpacing(8);
  top_widget->addLayout(minimum_buffer_widget, 0);
  top_widget->addSpacing(8);
  top_widget->addLayout(local_buffer_widget, 0);
  top_widget->addSpacing(8);
  top_widget->addStretch();
  top_widget->addWidget(m_quit_button, 0, Qt::AlignRight);

  bottom_widget->addWidget(m_save_sd_box);
  bottom_widget->addWidget(m_load_wii_box);
  bottom_widget->addWidget(m_sync_save_data_box);
  bottom_widget->addWidget(m_record_input_box);
  bottom_widget->addWidget(m_strict_settings_sync_box);
  bottom_widget->addWidget(m_host_input_authority_box);

  options_widget->addLayout(top_widget);
  options_widget->addLayout(bottom_widget);

  m_main_layout->addLayout(options_widget, 2, 0, 1, -1);
  m_main_layout->setRowStretch(1, 1000);

  setLayout(m_main_layout);
}

void NetPlayDialog::CreateChatLayout()
{
  m_chat_box = new QGroupBox(tr("Chat"));
  m_chat_edit = new QTextBrowser;
  m_chat_type_edit = new QLineEdit;
  m_chat_send_button = new QPushButton(tr("Send"));

  m_chat_send_button->setDefault(false);
  m_chat_send_button->setAutoDefault(false);

  m_chat_edit->setReadOnly(true);

  auto* layout = new QGridLayout;

  layout->addWidget(m_chat_edit, 0, 0, 1, -1);
  layout->addWidget(m_chat_type_edit, 1, 0);
  layout->addWidget(m_chat_send_button, 1, 1);

  m_chat_box->setLayout(layout);
}

void NetPlayDialog::CreatePlayersLayout()
{
  m_players_box = new QGroupBox(tr("Players"));
  m_room_box = new QComboBox;
  m_hostcode_label = new QLabel;
  m_hostcode_action_button = new QPushButton(tr("Copy"));
  m_longest_route_label = new QLabel;
  m_players_list = new QTableWidget;
  m_kick_button = new QPushButton(tr("Kick Player"));
  m_assign_ports_button = new QPushButton(tr("Assign Controller Ports"));

  m_players_list->setColumnCount(5);
  m_players_list->verticalHeader()->hide();
  m_players_list->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_players_list->horizontalHeader()->setStretchLastSection(true);

  for (int i = 0; i < 4; i++)
    m_players_list->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);

  auto* layout = new QGridLayout;

  layout->addWidget(m_room_box, 0, 0);
  layout->addWidget(m_hostcode_label, 0, 1);
  layout->addWidget(m_hostcode_action_button, 0, 2);
  layout->addWidget(m_longest_route_label, 1, 0, 1, -1);
  layout->addWidget(m_players_list, 2, 0, 1, -1);
  layout->addWidget(m_kick_button, 3, 0);
  layout->addWidget(m_assign_ports_button, 3, 1);

  m_players_box->setLayout(layout);
}

void NetPlayDialog::ConnectWidgets()
{
  // Players
  connect(m_room_box, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
          &NetPlayDialog::UpdateGUI);
  connect(m_hostcode_action_button, &QPushButton::clicked, [this] {
    if (m_is_copy_button_retry && m_room_box->currentIndex() == 0)
      g_TraversalClient->ReconnectToServer();
    else
      QApplication::clipboard()->setText(m_hostcode_label->text());
  });
  connect(m_players_list, &QTableWidget::itemSelectionChanged, [this] {
    int row = m_players_list->currentRow();
    m_kick_button->setEnabled(row > 0 &&
                              !m_players_list->currentItem()->data(Qt::UserRole).isNull());
  });
  connect(m_kick_button, &QPushButton::clicked, [this] {
    auto id = m_players_list->currentItem()->data(Qt::UserRole).toInt();
    Settings::Instance().GetNetPlayServer()->KickPlayer(id);
  });
  connect(m_assign_ports_button, &QPushButton::clicked, [this] {
    m_pad_mapping->exec();

    Settings::Instance().GetNetPlayServer()->SetPadMapping(m_pad_mapping->GetGCPadArray());
    Settings::Instance().GetNetPlayServer()->SetWiimoteMapping(m_pad_mapping->GetWiimoteArray());
  });

  // Chat
  connect(m_chat_send_button, &QPushButton::clicked, this, &NetPlayDialog::OnChat);
  connect(m_chat_type_edit, &QLineEdit::returnPressed, this, &NetPlayDialog::OnChat);

  // Other
  connect(m_minimum_buffer_size_box, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          [this](double value) {
            int ivalue = GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) ? (int)(value * 100) : (int)value;

            if (ivalue == m_minimum_buffer_size)
              return;

            auto server = Settings::Instance().GetNetPlayServer();
            if (server)
              server->AdjustMinimumPadBufferSize(ivalue);
          });

  connect(m_local_buffer_size_box, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
          [this](double value) {
            int ivalue = GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) ? (int)(value * 100) : (int)value;

            if (ivalue == m_local_buffer_size)
              return;

            auto client = Settings::Instance().GetNetPlayClient();
            if (client)
              client->AdjustLocalPadBufferSize(ivalue);
          });

  connect(m_auto_buffer_button, &QPushButton::clicked, this,
    [this] {
      m_auto_buffer_button->setEnabled(false);
      m_minimum_buffer_size_box->setEnabled(false);
      m_auto_buffer_sample_timer->start();
      SampleAutoBuffer();
    });

  connect(m_local_under_minimum_warning, &QPushButton::clicked, this,
    [this] {
      auto client = Settings::Instance().GetNetPlayClient();
      if (client)
        client->AdjustLocalPadBufferSize(m_minimum_buffer_size);
    });

  connect(m_host_input_authority_box, &QCheckBox::toggled, [this](bool checked) {
    auto server = Settings::Instance().GetNetPlayServer();
    if (server)
      server->SetHostInputAuthority(checked);
  });

  connect(m_start_button, &QPushButton::clicked, this, &NetPlayDialog::OnStart);
  connect(m_quit_button, &QPushButton::clicked, this, &NetPlayDialog::reject);

  connect(m_game_button, &QPushButton::clicked, [this] {
    GameListDialog gld(this);
    if (gld.exec() == QDialog::Accepted)
    {
      auto unique_id = gld.GetSelectedUniqueID();
      Settings::Instance().GetNetPlayServer()->ChangeGame(unique_id.toStdString());
    }
  });

  connect(&Settings::Instance(), &Settings::EmulationStateChanged, this, [=](Core::State state) {
    if (isVisible())
    {
      GameStatusChanged(state != Core::State::Uninitialized);
      if (state == Core::State::Uninitialized)
        DisplayMessage(tr("Stopped game"), "red");
    }
  });
}

void NetPlayDialog::OnChat()
{
  QueueOnObject(this, [this] {
    auto msg = m_chat_type_edit->text().toStdString();
    Settings::Instance().GetNetPlayClient()->SendChatMessage(msg);
    m_chat_type_edit->clear();

    DisplayMessage(QStringLiteral("%1: %2").arg(QString::fromStdString(m_nickname).toHtmlEscaped(),
                                                QString::fromStdString(msg).toHtmlEscaped()),
                   "#1d6ed8");
  });
}

void NetPlayDialog::OnStart()
{
  if (!Settings::Instance().GetNetPlayClient()->DoAllPlayersHaveGame())
  {
    if (QMessageBox::question(this, tr("Warning"),
                              tr("Not all players have the game. Do you really want to start?")) ==
        QMessageBox::No)
      return;
  }

  if (m_strict_settings_sync_box->isChecked() && Config::Get(Config::GFX_EFB_SCALE) == 0)
  {
    QMessageBox::critical(
        this, tr("Error"),
        tr("Auto internal resolution is not allowed in strict sync mode, as it depends on window "
           "size.\n\nPlease select a specific internal resolution."));
    return;
  }

  const auto game = FindGameFile(m_current_game);
  if (!game)
  {
    PanicAlertT("Selected game doesn't exist in game list!");
    return;
  }

  NetPlay::NetSettings settings;

  // Load GameINI so we can sync the settings from it
  Config::AddLayer(
      ConfigLoaders::GenerateGlobalGameConfigLoader(game->GetGameID(), game->GetRevision()));
  Config::AddLayer(
      ConfigLoaders::GenerateLocalGameConfigLoader(game->GetGameID(), game->GetRevision()));

  // Copy all relevant settings
  settings.m_CPUthread = Config::Get(Config::MAIN_CPU_THREAD);
  settings.m_CPUcore = Config::Get(Config::MAIN_CPU_CORE);
  settings.m_EnableCheats = Config::Get(Config::MAIN_ENABLE_CHEATS);
  settings.m_SelectedLanguage = Config::Get(Config::MAIN_GC_LANGUAGE);
  settings.m_OverrideGCLanguage = Config::Get(Config::MAIN_OVERRIDE_GC_LANGUAGE);
  settings.m_ProgressiveScan = Config::Get(Config::SYSCONF_PROGRESSIVE_SCAN);
  settings.m_PAL60 = Config::Get(Config::SYSCONF_PAL60);
  settings.m_DSPHLE = Config::Get(Config::MAIN_DSP_HLE);
  settings.m_DSPEnableJIT = Config::Get(Config::MAIN_DSP_JIT);
  settings.m_WriteToMemcard = m_save_sd_box->isChecked();
  settings.m_CopyWiiSave = m_load_wii_box->isChecked();
  settings.m_OCEnable = Config::Get(Config::MAIN_OVERCLOCK_ENABLE);
  settings.m_OCFactor = Config::Get(Config::MAIN_OVERCLOCK);
  settings.m_EXIDevice[0] =
      static_cast<ExpansionInterface::TEXIDevices>(Config::Get(Config::MAIN_SLOT_A));
  settings.m_EXIDevice[1] =
      static_cast<ExpansionInterface::TEXIDevices>(Config::Get(Config::MAIN_SLOT_B));
  settings.m_EFBAccessEnable = Config::Get(Config::GFX_HACK_EFB_ACCESS_ENABLE);
  settings.m_BBoxEnable = Config::Get(Config::GFX_HACK_BBOX_ENABLE);
  settings.m_ForceProgressive = Config::Get(Config::GFX_HACK_FORCE_PROGRESSIVE);
  settings.m_EFBToTextureEnable = Config::Get(Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
  settings.m_XFBToTextureEnable = Config::Get(Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
  settings.m_DisableCopyToVRAM = Config::Get(Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
  settings.m_ImmediateXFBEnable = Config::Get(Config::GFX_HACK_IMMEDIATE_XFB);
  settings.m_EFBEmulateFormatChanges = Config::Get(Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES);
  settings.m_SafeTextureCacheColorSamples =
      Config::Get(Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES);
  settings.m_PerfQueriesEnable = Config::Get(Config::GFX_PERF_QUERIES_ENABLE);
  settings.m_FPRF = Config::Get(Config::MAIN_FPRF);
  settings.m_AccurateNaNs = Config::Get(Config::MAIN_ACCURATE_NANS);
  settings.m_SyncOnSkipIdle = Config::Get(Config::MAIN_SYNC_ON_SKIP_IDLE);
  settings.m_SyncGPU = Config::Get(Config::MAIN_SYNC_GPU);
  settings.m_SyncGpuMaxDistance = Config::Get(Config::MAIN_SYNC_GPU_MAX_DISTANCE);
  settings.m_SyncGpuMinDistance = Config::Get(Config::MAIN_SYNC_GPU_MIN_DISTANCE);
  settings.m_SyncGpuOverclock = Config::Get(Config::MAIN_SYNC_GPU_OVERCLOCK);
  settings.m_JITFollowBranch = Config::Get(Config::MAIN_JIT_FOLLOW_BRANCH);
  settings.m_FastDiscSpeed = Config::Get(Config::MAIN_FAST_DISC_SPEED);
  settings.m_PollOnSIRead = Config::Get(Config::MAIN_POLL_ON_SIREAD);
  settings.m_MMU = Config::Get(Config::MAIN_MMU);
  settings.m_Fastmem = Config::Get(Config::MAIN_FASTMEM);
  settings.m_SkipIPL = Config::Get(Config::MAIN_SKIP_IPL) ||
                       !Settings::Instance().GetNetPlayServer()->DoAllPlayersHaveIPLDump();
  settings.m_LoadIPLDump = Config::Get(Config::MAIN_LOAD_IPL_DUMP) &&
                           Settings::Instance().GetNetPlayServer()->DoAllPlayersHaveIPLDump();
  settings.m_VertexRounding = Config::Get(Config::GFX_HACK_VERTEX_ROUDING);
  settings.m_InternalResolution = Config::Get(Config::GFX_EFB_SCALE);
  settings.m_EFBScaledCopy = Config::Get(Config::GFX_HACK_COPY_EFB_SCALED);
  settings.m_FastDepthCalc = Config::Get(Config::GFX_FAST_DEPTH_CALC);
  settings.m_EnablePixelLighting = Config::Get(Config::GFX_ENABLE_PIXEL_LIGHTING);
  settings.m_WidescreenHack = Config::Get(Config::GFX_WIDESCREEN_HACK);
  settings.m_ForceFiltering = Config::Get(Config::GFX_ENHANCE_FORCE_FILTERING);
  settings.m_MaxAnisotropy = Config::Get(Config::GFX_ENHANCE_MAX_ANISOTROPY);
  settings.m_ForceTrueColor = Config::Get(Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
  settings.m_DisableCopyFilter = Config::Get(Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
  settings.m_DisableFog = Config::Get(Config::GFX_DISABLE_FOG);
  settings.m_ArbitraryMipmapDetection = Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION);
  settings.m_ArbitraryMipmapDetectionThreshold =
      Config::Get(Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION_THRESHOLD);
  settings.m_EnableGPUTextureDecoding = Config::Get(Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
  settings.m_StrictSettingsSync = m_strict_settings_sync_box->isChecked();
  settings.m_SyncSaveData = m_sync_save_data_box->isChecked();

  // Unload GameINI to restore things to normal
  Config::RemoveLayer(Config::LayerType::GlobalGame);
  Config::RemoveLayer(Config::LayerType::LocalGame);

  Settings::Instance().GetNetPlayServer()->SetNetSettings(settings);
  if (Settings::Instance().GetNetPlayServer()->RequestStartGame())
    SetOptionsEnabled(false);
}

void NetPlayDialog::reject()
{
  if (QMessageBox::question(this, tr("Confirmation"),
                            tr("Are you sure you want to quit NetPlay?")) == QMessageBox::Yes)
  {
    QDialog::reject();
  }
}

void NetPlayDialog::show(std::string nickname, bool use_traversal)
{
  m_nickname = nickname;
  m_use_traversal = use_traversal;
  m_old_player_count = 0;

  m_room_box->clear();
  m_chat_edit->clear();
  m_chat_type_edit->clear();

  bool is_hosting = Settings::Instance().GetNetPlayServer() != nullptr;

  if (is_hosting)
  {
    if (use_traversal)
      m_room_box->addItem(tr("Room ID"));

    for (const auto& iface : Settings::Instance().GetNetPlayServer()->GetInterfaceSet())
    {
      const auto interface = QString::fromStdString(iface);
      m_room_box->addItem(iface == "!local!" ? tr("Local") : interface, interface);
    }
  }

  m_start_button->setHidden(!is_hosting);
  m_save_sd_box->setHidden(!is_hosting);
  m_load_wii_box->setHidden(!is_hosting);
  m_sync_save_data_box->setHidden(!is_hosting);
  m_strict_settings_sync_box->setHidden(!is_hosting);
  m_host_input_authority_box->setHidden(!is_hosting);
  m_kick_button->setHidden(!is_hosting);
  m_assign_ports_button->setHidden(!is_hosting);
  m_md5_button->setHidden(!is_hosting);
  m_room_box->setHidden(!is_hosting);
  m_hostcode_label->setHidden(!is_hosting);
  m_hostcode_action_button->setHidden(!is_hosting);
  m_longest_route_label->setHidden(!is_hosting);
  m_game_button->setEnabled(is_hosting);
  m_kick_button->setEnabled(false);
  m_auto_buffer_button->setHidden(!is_hosting);
  m_auto_buffer_button->setEnabled(MeetsAutoBufferConditions());
  m_minimum_buffer_label->setHidden(!is_hosting);
  m_minimum_buffer_size_box->setHidden(!is_hosting);

  QDialog::show();
  UpdateGUI();
}

void NetPlayDialog::UpdateDiscordPresence()
{
#ifdef USE_DISCORD_PRESENCE
  // both m_current_game and m_player_count need to be set for the status to be displayed correctly
  if (m_player_count == 0 || m_current_game.empty())
    return;

  const auto use_default = [this]() {
    Discord::UpdateDiscordPresence(m_player_count, Discord::SecretType::Empty, "", m_current_game);
  };

  if (Core::IsRunning())
    return use_default();

  if (IsHosting())
  {
    if (g_TraversalClient)
    {
      const auto host_id = g_TraversalClient->GetHostID();
      if (host_id[0] == '\0')
        return use_default();

      Discord::UpdateDiscordPresence(m_player_count, Discord::SecretType::RoomID,
                                     std::string(host_id.begin(), host_id.end()), m_current_game);
    }
    else
    {
      if (m_external_ip_address.empty())
      {
        Common::HttpRequest request;
        // ENet does not support IPv6, so IPv4 has to be used
        request.UseIPv4();
        Common::HttpRequest::Response response =
            request.Get("https://ip.dolphin-emu.org/", {{"X-Is-Dolphin", "1"}});

        if (!response.has_value())
          return use_default();
        m_external_ip_address = std::string(response->begin(), response->end());
      }
      const int port = Settings::Instance().GetNetPlayServer()->GetPort();

      Discord::UpdateDiscordPresence(
          m_player_count, Discord::SecretType::IPAddress,
          Discord::CreateSecretFromIPAddress(m_external_ip_address, port), m_current_game);
    }
  }
  else
  {
    use_default();
  }
#endif
}

void NetPlayDialog::UpdateGUI()
{
  auto client = Settings::Instance().GetNetPlayClient();
  auto server = Settings::Instance().GetNetPlayServer();
  if (!client)
    return;

  // Update Player List
  const auto players = client->GetPlayers();

  if (static_cast<int>(players.size()) != m_player_count && m_player_count != 0)
    QApplication::alert(this);

  m_player_count = static_cast<int>(players.size());

  int selection_pid = m_players_list->currentItem() ?
                          m_players_list->currentItem()->data(Qt::UserRole).toInt() :
                          -1;

  m_players_list->clear();
  m_players_list->setHorizontalHeaderLabels(
      {tr("Player"), tr("Game Status"), tr("Ping"), tr("Mapping"), tr("Revision")});
  m_players_list->setRowCount(m_player_count);

  const auto get_mapping_string = [](const NetPlay::Player* player,
                                     const NetPlay::PadMappingArray& array) {
    std::string str;
    for (size_t i = 0; i < array.size(); i++)
    {
      if (player->pid == array[i])
        str += std::to_string(i + 1);
      else
        str += '-';
    }

    return '|' + str + '|';
  };

  static const std::map<NetPlay::PlayerGameStatus, QString> player_status{
      {NetPlay::PlayerGameStatus::Ok, tr("OK")},
      {NetPlay::PlayerGameStatus::NotFound, tr("Not Found")},
  };

  for (int i = 0; i < m_player_count; i++)
  {
    const auto* p = players[i];

    auto* name_item = new QTableWidgetItem(QString::fromStdString(p->name));
    auto* status_item = new QTableWidgetItem(player_status.count(p->game_status) ?
                                                 player_status.at(p->game_status) :
                                                 QStringLiteral("?"));
    auto* ping_item = new QTableWidgetItem(QStringLiteral("%1 ms").arg(p->ping));
    auto* mapping_item = new QTableWidgetItem(
        QString::fromStdString(get_mapping_string(p, client->GetPadMapping()) +
                               get_mapping_string(p, client->GetWiimoteMapping())));
    auto* revision_item = new QTableWidgetItem(QString::fromStdString(p->revision));

    for (auto* item : {name_item, status_item, ping_item, mapping_item, revision_item})
    {
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
      item->setData(Qt::UserRole, static_cast<int>(p->pid));
    }

    m_players_list->setItem(i, 0, name_item);
    m_players_list->setItem(i, 1, status_item);
    m_players_list->setItem(i, 2, ping_item);
    m_players_list->setItem(i, 3, mapping_item);
    m_players_list->setItem(i, 4, revision_item);

    if (p->pid == selection_pid)
      m_players_list->selectRow(i);
  }

  // Update Room ID / IP label
  if (m_use_traversal && m_room_box->currentIndex() == 0)
  {
    switch (g_TraversalClient->GetState())
    {
    case TraversalClient::Connecting:
      m_hostcode_label->setText(tr("..."));
      m_hostcode_action_button->setEnabled(false);
      break;
    case TraversalClient::Connected:
    {
      const auto host_id = g_TraversalClient->GetHostID();
      m_hostcode_label->setText(
          QString::fromStdString(std::string(host_id.begin(), host_id.end())));
      m_hostcode_action_button->setEnabled(true);
      m_hostcode_action_button->setText(tr("Copy"));
      m_is_copy_button_retry = false;
      break;
    }
    case TraversalClient::Failure:
      m_hostcode_label->setText(tr("Error"));
      m_hostcode_action_button->setText(tr("Retry"));
      m_hostcode_action_button->setEnabled(true);
      m_is_copy_button_retry = true;
      break;
    }
  }
  else if (server)
  {
    m_hostcode_label->setText(QString::fromStdString(
        server->GetInterfaceHost(m_room_box->currentData().toString().toStdString())));
    m_hostcode_action_button->setText(tr("Copy"));
    m_hostcode_action_button->setEnabled(true);
  }

  if (m_old_player_count != m_player_count)
  {
    UpdateDiscordPresence();
    m_old_player_count = m_player_count;
  }

  if(IsHosting())
  {
    NetPlay::NetPlayServer::NetRoute longest_route = Settings::Instance().GetNetPlayServer()->FindLongestRoute();

    if(longest_route.from == nullptr)
      m_longest_route_label->setText(tr("Longest route over network: (none)"));
    else if(longest_route.to == nullptr)
      m_longest_route_label->setText(tr("Longest route over network: %1 \u2192 Server (%3 ms)").
        arg(QString::fromStdString(longest_route.from->name)).
        arg(longest_route.ping));
    else
      m_longest_route_label->setText(tr("Longest route over network: %1 \u2192 Server \u2192 %2 (%3 ms)").
        arg(QString::fromStdString(longest_route.from->name)).
        arg(QString::fromStdString(longest_route.to->name)).
        arg(longest_route.ping));
  }
}

// NetPlayUI methods

void NetPlayDialog::BootGame(const std::string& filename)
{
  m_got_stop_request = false;
  emit Boot(QString::fromStdString(filename));
}

void NetPlayDialog::StopGame()
{
  if (m_got_stop_request)
    return;

  m_got_stop_request = true;
  emit Stop();
}

bool NetPlayDialog::IsHosting() const
{
  return Settings::Instance().GetNetPlayServer() != nullptr;
}

void NetPlayDialog::Update()
{
  QueueOnObject(this, &NetPlayDialog::UpdateGUI);
}

void NetPlayDialog::DisplayMessage(const QString& msg, const std::string& color, int duration)
{
  QueueOnObject(m_chat_edit, [this, color, msg] {
    m_chat_edit->append(
        QStringLiteral("<font color='%1'>%2</font>").arg(QString::fromStdString(color), msg));
  });

  if (g_ActiveConfig.bShowNetPlayMessages && Core::IsRunning())
  {
    u32 osd_color;

    // Convert the color string to a OSD color
    if (color == "red")
      osd_color = OSD::Color::RED;
    else if (color == "cyan")
      osd_color = OSD::Color::CYAN;
    else if (color == "green")
      osd_color = OSD::Color::GREEN;
    else
      osd_color = OSD::Color::YELLOW;

    OSD::AddTypedMessage(OSD::MessageType::NetPlayBuffer, msg.toStdString(), OSD::Duration::NORMAL,
                         osd_color);
  }
}

void NetPlayDialog::AppendChat(const std::string& msg)
{
  DisplayMessage(QString::fromStdString(msg).toHtmlEscaped(), "");
  QApplication::alert(this);
}

void NetPlayDialog::OnMsgChangeGame(const std::string& title)
{
  QString qtitle = QString::fromStdString(title);
  QueueOnObject(this, [this, qtitle, title] {
    m_game_button->setText(qtitle);
    m_current_game = title;
    UpdateDiscordPresence();

    if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) && !m_host_input_authority)
    {
      m_minimum_buffer_size_box->setDecimals(2); m_local_buffer_size_box->setDecimals(2);
      m_minimum_buffer_size_box->setSingleStep(0.25); m_local_buffer_size_box->setSingleStep(0.25);
      m_minimum_buffer_size_box->setSuffix(tr(" frame(s)")); m_local_buffer_size_box->setSuffix(tr(" frame(s)"));
    }
    else
    {
      m_minimum_buffer_size_box->setSingleStep(1); m_local_buffer_size_box->setSingleStep(1);
      m_minimum_buffer_size_box->setDecimals(0); m_local_buffer_size_box->setDecimals(0);
      m_minimum_buffer_size_box->setSuffix(QString::fromStdString("")); m_local_buffer_size_box->setSuffix(QString::fromStdString(""));
    }

    m_auto_buffer_button->setEnabled(MeetsAutoBufferConditions());

    Settings::Instance().GetNetPlayClient()->AdjustLocalPadBufferSize(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) ? 150 : 6);
    if(Settings::Instance().GetNetPlayServer())
        Settings::Instance().GetNetPlayServer()->AdjustMinimumPadBufferSize(0);
  });

  DisplayMessage(tr("Game changed to \"%1\"").arg(qtitle), "magenta");
}

void NetPlayDialog::GameStatusChanged(bool running)
{
  if (!running && !m_got_stop_request)
    Settings::Instance().GetNetPlayClient()->RequestStopGame();

  QueueOnObject(this, [this, running] { SetOptionsEnabled(!running); });
}

void NetPlayDialog::SetOptionsEnabled(bool enabled)
{
  if (Settings::Instance().GetNetPlayServer())
  {
    m_start_button->setEnabled(enabled);
    m_game_button->setEnabled(enabled);
    m_load_wii_box->setEnabled(enabled);
    m_save_sd_box->setEnabled(enabled);
    m_sync_save_data_box->setEnabled(enabled);
    m_assign_ports_button->setEnabled(enabled);
    m_strict_settings_sync_box->setEnabled(enabled);
    m_host_input_authority_box->setEnabled(enabled);
  }

  m_record_input_box->setEnabled(enabled);
}

void NetPlayDialog::OnMsgStartGame()
{
  DisplayMessage(tr("Started game"), "green");

  QueueOnObject(this, [this] {
    auto client = Settings::Instance().GetNetPlayClient();
    if (client)
      client->StartGame(FindGame(m_current_game));
    UpdateDiscordPresence();
  });
}

void NetPlayDialog::OnMsgStopGame()
{
  QueueOnObject(this, [this] { UpdateDiscordPresence(); });
}

void NetPlayDialog::OnMinimumPadBufferChanged(u32 buffer)
{
  QueueOnObject(this, [this, buffer] {
    const QSignalBlocker blocker(m_minimum_buffer_size_box);

    if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD))
      m_minimum_buffer_size_box->setValue(buffer / 100.0);
    else
      m_minimum_buffer_size_box->setValue(buffer);
  });

  if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD))
  {
    QString frame_str = buffer == 100 ? tr("frame") : tr("frames");
    DisplayMessage(tr("Minimum buffer size changed to %1 %2").arg(buffer / 100.0).arg(frame_str), "");
  }
  else
  {
    DisplayMessage(tr("Minimum buffer size changed to %1").arg(buffer), "");
  }

  m_minimum_buffer_size = static_cast<int>(buffer);
  m_local_under_minimum_warning->setHidden(m_local_buffer_size >= m_minimum_buffer_size || m_host_input_authority);
}

void NetPlayDialog::OnLocalPadBufferChanged(u32 buffer)
{
  QueueOnObject(this, [this, buffer] {
    const QSignalBlocker blocker(m_local_buffer_size_box);

    if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD))
      m_local_buffer_size_box->setValue(buffer / 100.0);
    else
      m_local_buffer_size_box->setValue(buffer);
  });

  if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD))
  {
    QString frame_str = buffer == 100 ? tr("frame") : tr("frames");

    DisplayMessage(tr("Buffer size changed to %1 %2").arg(buffer / 100.0).arg(frame_str), "");
  }
  else
  {
    DisplayMessage(tr("Buffer size changed to %1").arg(buffer), "");
  }

  m_local_buffer_size = static_cast<int>(buffer);
  m_local_under_minimum_warning->setHidden(m_local_buffer_size >= m_minimum_buffer_size || m_host_input_authority);
}

void NetPlayDialog::OnHostInputAuthorityChanged(bool enabled)
{
  QueueOnObject(this, [this, enabled] {
    const bool is_hosting = IsHosting();
    const bool enable_buffer = is_hosting != enabled;

    if (is_hosting)
    {
      m_minimum_buffer_size_box->setEnabled(enable_buffer);
      m_local_buffer_size_box->setEnabled(enable_buffer);
      m_auto_buffer_button->setEnabled(MeetsAutoBufferConditions() && enable_buffer);

      QSignalBlocker blocker(m_host_input_authority_box);
      m_host_input_authority_box->setChecked(enabled);
    }
  });

  DisplayMessage(enabled ? tr("Host input authority enabled") : tr("Host input authority disabled"),
                 "");

  m_host_input_authority = enabled;
  m_local_under_minimum_warning->setHidden(m_local_buffer_size >= m_minimum_buffer_size || m_host_input_authority);

  if(GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD) && !m_host_input_authority)
  {
    m_minimum_buffer_size_box->setDecimals(2); m_local_buffer_size_box->setDecimals(2);
    m_minimum_buffer_size_box->setSingleStep(0.25); m_local_buffer_size_box->setSingleStep(0.25);
    m_minimum_buffer_size_box->setSuffix(tr(" frame(s)")); m_local_buffer_size_box->setSuffix(tr(" frame(s)"));
  }
  else
  {
    m_minimum_buffer_size_box->setSingleStep(1); m_local_buffer_size_box->setSingleStep(1);
    m_minimum_buffer_size_box->setDecimals(0); m_local_buffer_size_box->setDecimals(0);
    m_minimum_buffer_size_box->setSuffix(QString::fromStdString("")); m_local_buffer_size_box->setSuffix(QString::fromStdString(""));
  }
}

void NetPlayDialog::OnDesync(u32 frame, const std::string& player)
{
  DisplayMessage(tr("Possible desync detected: %1 might have desynced at frame %2")
                     .arg(QString::fromStdString(player), QString::number(frame)),
                 "red", OSD::Duration::VERY_LONG);
}

void NetPlayDialog::OnConnectionLost()
{
  DisplayMessage(tr("Lost connection to NetPlay server..."), "red");
}

void NetPlayDialog::OnConnectionError(const std::string& message)
{
  QueueOnObject(this, [this, message] {
    QMessageBox::critical(this, tr("Error"),
                          tr("Failed to connect to server: %1").arg(tr(message.c_str())));
  });
}

void NetPlayDialog::OnTraversalError(TraversalClient::FailureReason error)
{
  QueueOnObject(this, [this, error] {
    switch (error)
    {
    case TraversalClient::FailureReason::BadHost:
      QMessageBox::critical(this, tr("Traversal Error"), tr("Couldn't look up central server"));
      QDialog::reject();
      break;
    case TraversalClient::FailureReason::VersionTooOld:
      QMessageBox::critical(this, tr("Traversal Error"),
                            tr("Dolphin is too old for traversal server"));
      QDialog::reject();
      break;
    case TraversalClient::FailureReason::ServerForgotAboutUs:
    case TraversalClient::FailureReason::SocketSendError:
    case TraversalClient::FailureReason::ResendTimeout:
      UpdateGUI();
      break;
    }
  });
}

void NetPlayDialog::OnTraversalStateChanged(TraversalClient::State state)
{
  switch (state)
  {
  case TraversalClient::State::Connected:
  case TraversalClient::State::Failure:
    UpdateDiscordPresence();
  default:
    break;
  }
}

void NetPlayDialog::OnSaveDataSyncFailure()
{
  QueueOnObject(this, [this] { SetOptionsEnabled(true); });
}

bool NetPlayDialog::IsRecording()
{
  std::optional<bool> is_recording = RunOnObject(m_record_input_box, &QCheckBox::isChecked);
  if (is_recording)
    return *is_recording;
  return false;
}

std::string NetPlayDialog::FindGame(const std::string& game)
{
  std::optional<std::string> path = RunOnObject(this, [this, &game] {
    for (int i = 0; i < m_game_list_model->rowCount(QModelIndex()); i++)
    {
      if (m_game_list_model->GetUniqueIdentifier(i).toStdString() == game)
        return m_game_list_model->GetPath(i).toStdString();
    }
    return std::string("");
  });
  if (path)
    return *path;
  return std::string("");
}

std::shared_ptr<const UICommon::GameFile> NetPlayDialog::FindGameFile(const std::string& game)
{
  std::optional<std::shared_ptr<const UICommon::GameFile>> game_file =
      RunOnObject(this, [this, &game] {
        for (int i = 0; i < m_game_list_model->rowCount(QModelIndex()); i++)
        {
          if (m_game_list_model->GetUniqueIdentifier(i).toStdString() == game)
            return m_game_list_model->GetGameFile(i);
        }
        return static_cast<std::shared_ptr<const UICommon::GameFile>>(nullptr);
      });
  if (game_file)
    return *game_file;
  return nullptr;
}

void NetPlayDialog::ShowMD5Dialog(const std::string& file_identifier)
{
  QueueOnObject(this, [this, file_identifier] {
    m_md5_button->setEnabled(false);

    if (m_md5_dialog->isVisible())
      m_md5_dialog->close();

    m_md5_dialog->show(QString::fromStdString(file_identifier));
  });
}

void NetPlayDialog::SetMD5Progress(int pid, int progress)
{
  QueueOnObject(this, [this, pid, progress] {
    if (m_md5_dialog->isVisible())
      m_md5_dialog->SetProgress(pid, progress);
  });
}

void NetPlayDialog::SetMD5Result(int pid, const std::string& result)
{
  QueueOnObject(this, [this, pid, result] {
    m_md5_dialog->SetResult(pid, result);
    m_md5_button->setEnabled(true);
  });
}

void NetPlayDialog::AbortMD5()
{
  QueueOnObject(this, [this] {
    m_md5_dialog->close();
    m_md5_button->setEnabled(true);
  });
}

bool NetPlayDialog::MeetsAutoBufferConditions()
{
  return FindGameFile(m_current_game) != nullptr &&
    // TODO: Melee PAL60
    FindGameFile(m_current_game)->GetRegion() != DiscIO::Region::PAL &&
    GetConfigOptionWithSelectedGame(Config::MAIN_POLL_ON_SIREAD);
}

bool NetPlayDialog::CalculateBufferFromSamples(const std::vector<NetPlay::NetPlayServer::NetRoute>& samples)
{
  int first_sample = samples[0].ping;
  int sample_difference = first_sample * (int)samples.size();

  for(int i = 1; i < samples.size(); i++)
    sample_difference -= samples[i].ping;

  sample_difference = std::abs(sample_difference);

  if(sample_difference < 100)
  {
    int average_ping = 0;
    for(int i = 0; i < samples.size(); i++)
      average_ping += samples[i].ping;

    average_ping /= (int)samples.size();

    DisplayMessage(tr("Average ping out of 3 samples was %1 ms").arg(average_ping), "green");

    int auto_buffer = (int)(((average_ping * 1.1) / (1000.0 / 60.0)) * 100);
    Settings::Instance().GetNetPlayServer()->AdjustMinimumPadBufferSize(auto_buffer);
    return true;
  }

  return false;
}

void NetPlayDialog::SampleAutoBuffer()
{
  m_auto_buffer_samples.push_back(Settings::Instance().GetNetPlayServer()->FindLongestRoute());
  m_auto_buffer_button->setText(tr("Auto (%1 s)").arg(m_auto_buffer_sample_amount - m_auto_buffer_samples.size()));

  NetPlay::NetPlayServer::NetRoute latest_sample = m_auto_buffer_samples[m_auto_buffer_samples.size() - 1];

  if(latest_sample.from == nullptr || latest_sample.to == nullptr)
  {
    DisplayMessage(tr("Unable to calculate auto buffer (at least 2 players are required)"), "red");
    m_auto_buffer_button->setText(tr("Auto"));
    m_auto_buffer_button->setEnabled(MeetsAutoBufferConditions());
    m_minimum_buffer_size_box->setEnabled(true);
    m_auto_buffer_samples.clear();
    m_auto_buffer_sample_timer->stop();
    return;
  }

  DisplayMessage(tr("Sample %1/%2 - %3 \u2192 Server \u2192 %4 at %5 ms").
    arg(m_auto_buffer_samples.size()).
    arg(m_auto_buffer_sample_amount).
    arg(QString::fromStdString(latest_sample.from->name)).
    arg(QString::fromStdString(latest_sample.to->name)).
    arg(latest_sample.ping), "green");

  if(m_auto_buffer_samples.size() >= m_auto_buffer_sample_amount)
  {
    if(!CalculateBufferFromSamples(m_auto_buffer_samples))
        DisplayMessage(tr("Unable to calculate auto buffer because the ping times were too unstable."), "red");

    m_auto_buffer_button->setText(tr("Auto"));
    m_auto_buffer_button->setEnabled(MeetsAutoBufferConditions());
    m_minimum_buffer_size_box->setEnabled(true);
    m_auto_buffer_samples.clear();
    m_auto_buffer_sample_timer->stop();
    return;
  }
}
