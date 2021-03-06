#include "common/common_pch.h"

#include <QDebug>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>

#include "common/chapters/chapters.h"
#include "common/extern_data.h"
#include "common/fs_sys_helpers.h"
#include "common/iso639.h"
#include "common/qt.h"
#include "common/version.h"
#include "mkvtoolnix-gui/app.h"
#include "mkvtoolnix-gui/jobs/program_runner.h"
#include "mkvtoolnix-gui/merge/enums.h"
#include "mkvtoolnix-gui/merge/source_file.h"
#include "mkvtoolnix-gui/util/file_dialog.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/settings_names.h"
#include "mkvtoolnix-gui/util/string.h"
#include "mkvtoolnix-gui/util/widget.h"

namespace mtx { namespace gui { namespace Util {

QString
Settings::RunProgramConfig::validate()
  const {
  return (m_type == RunProgramType::ExecuteProgram) && (m_commandLine.isEmpty() || m_commandLine.value(0).isEmpty()) ? QY("The program to execute hasn't been set yet.")
       : (m_type == RunProgramType::PlayAudioFile)  && m_audioFile.isEmpty()                                         ? QY("The audio file to play hasn't been set yet.")
       :                                                                                                               QString{};
}

bool
Settings::RunProgramConfig::isValid()
  const {
  return validate().isEmpty();
}

QString
Settings::RunProgramConfig::name()
  const {
  if (!m_name.isEmpty())
    return m_name;

  return m_type == RunProgramType::ExecuteProgram    ? nameForExternalProgram()
       : m_type == RunProgramType::PlayAudioFile     ? nameForPlayAudioFile()
       : m_type == RunProgramType::ShutDownComputer  ? QY("Shut down the computer")
       : m_type == RunProgramType::HibernateComputer ? QY("Hibernate the computer")
       : m_type == RunProgramType::SleepComputer     ? QY("Sleep the computer")
       :                                               Q("unknown");
}

QString
Settings::RunProgramConfig::nameForExternalProgram()
  const {
  if (m_commandLine.isEmpty())
    return QY("Execute a program");

  auto program = m_commandLine.value(0);
  program.replace(QRegularExpression{Q(".*[/\\\\]")}, Q(""));

  return QY("Execute program '%1'").arg(program);
}

QString
Settings::RunProgramConfig::nameForPlayAudioFile()
  const {
  if (m_audioFile.isEmpty())
    return QY("Play an audio file");

  auto audioFile = m_audioFile;
  audioFile.replace(QRegularExpression{Q(".*[/\\\\]")}, Q(""));

  return QY("Play audio file '%1'").arg(audioFile);
}

Settings Settings::s_settings;

Settings::Settings() {
}

void
Settings::change(std::function<void(Settings &)> worker) {
  worker(s_settings);
  s_settings.save();
}

Settings &
Settings::get() {
  return s_settings;
}

QString
Settings::iniFileLocation() {
#if defined(SYS_WINDOWS)
  if (!App::isInstalled())
    // QApplication::applicationDirPath() cannot be used here as the
    // Util::Settings class might be used before QCoreApplication's
    // been instantiated, which QApplication::applicationDirPath()
    // requires.
    return Q(mtx::sys::get_current_exe_path("").string());

  return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
#else
  return Q("%1/%2/%3").arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).arg(App::organizationName()).arg(App::applicationName());
#endif
}

QString
Settings::cacheDirLocation(QString const &subDir) {
  QString dir;

#if defined(SYS_WINDOWS)
  if (!App::isInstalled())
    dir = Q("%1/cache").arg(App::applicationDirPath());
#endif

  if (dir.isEmpty())
    dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

  if (!subDir.isEmpty())
    dir = Q("%1/%2").arg(dir).arg(subDir);

  return QDir::toNativeSeparators(dir);
}

QString
Settings::prepareCacheDir(QString const &subDir) {
  auto dirName = cacheDirLocation(subDir);
  QDir{dirName}.mkpath(dirName);

  return QDir::toNativeSeparators(dirName);
}

QString
Settings::iniFileName() {
  return Q("%1/mkvtoolnix-gui.ini").arg(iniFileLocation());
}

void
Settings::migrateFromRegistry() {
#if defined(SYS_WINDOWS)
  // Migration of settings from the Windows registry into an .ini file
  // due to performance issues.

  // If this is the portable version the no settings have to be migrated.
  if (!App::isInstalled())
    return;

  // Don't do anything if such a file exists already.
  auto targetFileName = iniFileName();
  if (QFileInfo{targetFileName}.exists())
    return;

  // Copy all settings.
  QSettings target{targetFileName, QSettings::IniFormat};
  QSettings source{};

  for (auto const &key : source.allKeys())
    target.setValue(key, source.value(key));

  // Ensure the new file is written and remove the keys from the
  // registry.
  target.sync();
  source.clear();
  source.sync();

#else
  // Rename file from .conf to .ini in order to be consistent.
  auto target = QFileInfo{ iniFileName() };
  auto source = QFileInfo{ Q("%1/%2.conf").arg(target.absolutePath()).arg(target.baseName()) };

  if (source.exists())
    QFile{ source.absoluteFilePath() }.rename(target.filePath());
#endif
}

std::unique_ptr<QSettings>
Settings::registry() {
  return std::make_unique<QSettings>(iniFileName(), QSettings::IniFormat);
}

void
Settings::convertOldSettings() {
  auto reg = registry();

  // mergeAlwaysAddDroppedFiles → mergeAddingAppendingFilesPolicy
  reg->beginGroup(s_grpSettings);
  auto mergeAlwaysAddDroppedFiles = reg->value(s_valMergeAlwaysAddDroppedFiles);

  if (mergeAlwaysAddDroppedFiles.isValid())
    reg->setValue(s_valMergeAddingAppendingFilesPolicy, static_cast<int>(MergeAddingAppendingFilesPolicy::Add));

  reg->remove(s_valMergeAlwaysAddDroppedFiles);
  reg->endGroup();

  // defaultTrackLanguage → defaultAudioTrackLanguage, defaultVideoTrackLanguage, defaultSubtitleTrackLanguage;
  reg->beginGroup(s_grpSettings);

  auto defaultTrackLanguage = reg->value(s_valDefaultTrackLanguage);
  reg->remove(s_valDefaultTrackLanguage);

  if (defaultTrackLanguage.isValid()) {
    reg->setValue(s_valDefaultAudioTrackLanguage,    defaultTrackLanguage.toString());
    reg->setValue(s_valDefaultVideoTrackLanguage,    defaultTrackLanguage.toString());
    reg->setValue(s_valDefaultSubtitleTrackLanguage, defaultTrackLanguage.toString());
  }

  // mergeUseVerticalInputLayout → mergeTrackPropertiesLayout
  auto mergeUseVerticalInputLayout = reg->value(s_valMergeUseVerticalInputLayout);
  reg->remove(s_valMergeUseVerticalInputLayout);

  if (mergeUseVerticalInputLayout.isValid())
    reg->setValue(s_valMergeTrackPropertiesLayout, static_cast<int>(mergeUseVerticalInputLayout.toBool() ? TrackPropertiesLayout::VerticalTabWidget : TrackPropertiesLayout::HorizontalScrollArea));

  reg->endGroup();
}

void
Settings::load() {
  convertOldSettings();

  auto regPtr      = registry();
  auto &reg        = *regPtr;
  auto defaultFont = defaultUiFont();
  boost::optional<QVariant> enableMuxingTracksByTheseTypes;

  reg.beginGroup(s_grpInfo);
  auto guiVersion                      = reg.value(s_valGuiVersion).toString();
  reg.endGroup();

  reg.beginGroup(s_grpSettings);
  m_priority                           = static_cast<ProcessPriority>(reg.value(s_valPriority, static_cast<int>(NormalPriority)).toInt());
  m_probeRangePercentage               = reg.value(s_valProbeRangePercentage, 0.3).toDouble();
  m_tabPosition                        = static_cast<QTabWidget::TabPosition>(reg.value(s_valTabPosition, static_cast<int>(QTabWidget::North)).toInt());
  m_lastOpenDir                        = QDir{reg.value(s_valLastOpenDir).toString()};
  m_lastOutputDir                      = QDir{reg.value(s_valLastOutputDir).toString()};
  m_lastConfigDir                      = QDir{reg.value(s_valLastConfigDir).toString()};

  m_oftenUsedLanguages                 = reg.value(s_valOftenUsedLanguages).toStringList();
  m_oftenUsedCountries                 = reg.value(s_valOftenUsedCountries).toStringList();
  m_oftenUsedCharacterSets             = reg.value(s_valOftenUsedCharacterSets).toStringList();

  m_oftenUsedLanguagesOnly             = reg.value(s_valOftenUsedLanguagesOnly,     false).toBool();;
  m_oftenUsedCountriesOnly             = reg.value(s_valOftenUsedCountriesOnly,     false).toBool();;
  m_oftenUsedCharacterSetsOnly         = reg.value(s_valOftenUsedCharacterSetsOnly, false).toBool();;

  m_scanForPlaylistsPolicy             = static_cast<ScanForPlaylistsPolicy>(reg.value(s_valScanForPlaylistsPolicy, static_cast<int>(AskBeforeScanning)).toInt());
  m_minimumPlaylistDuration            = reg.value(s_valMinimumPlaylistDuration, 120).toUInt();

  m_setAudioDelayFromFileName          = reg.value(s_valSetAudioDelayFromFileName, true).toBool();
  m_autoSetFileTitle                   = reg.value(s_valAutoSetFileTitle,          true).toBool();
  m_autoClearFileTitle                 = reg.value(s_valAutoClearFileTitle,        m_autoSetFileTitle).toBool();
  m_clearMergeSettings                 = static_cast<ClearMergeSettingsAction>(reg.value(s_valClearMergeSettings, static_cast<int>(ClearMergeSettingsAction::None)).toInt());
  m_disableCompressionForAllTrackTypes = reg.value(s_valDisableCompressionForAllTrackTypes, false).toBool();
  m_disableDefaultTrackForSubtitles    = reg.value(s_valDisableDefaultTrackForSubtitles,    false).toBool();
  m_mergeEnableDialogNormGainRemoval   = reg.value(s_valMergeEnableDialogNormGainRemoval,   false).toBool();
  m_mergeAlwaysShowOutputFileControls  = reg.value(s_valMergeAlwaysShowOutputFileControls,  true).toBool();
  m_mergePredefinedTrackNames          = reg.value(s_valMergePredefinedTrackNames).toStringList();
  m_mergePredefinedSplitSizes          = reg.value(s_valMergePredefinedSplitSizes).toStringList();
  m_mergePredefinedSplitDurations      = reg.value(s_valMergePredefinedSplitDurations).toStringList();
  m_mergeTrackPropertiesLayout         = static_cast<TrackPropertiesLayout>(reg.value(s_valMergeTrackPropertiesLayout, static_cast<int>(TrackPropertiesLayout::HorizontalScrollArea)).toInt());
  m_mergeAddingAppendingFilesPolicy    = static_cast<MergeAddingAppendingFilesPolicy>(reg.value(s_valMergeAddingAppendingFilesPolicy, static_cast<int>(MergeAddingAppendingFilesPolicy::Ask)).toInt());
  m_mergeLastAddingAppendingDecision   = static_cast<MergeAddingAppendingFilesPolicy>(reg.value(s_valMergeLastAddingAppendingDecision, static_cast<int>(MergeAddingAppendingFilesPolicy::Add)).toInt());
  m_mergeWarnMissingAudioTrack         = static_cast<MergeMissingAudioTrackPolicy>(reg.value(s_valMergeWarnMissingAudioTrack, static_cast<int>(MergeMissingAudioTrackPolicy::IfAudioTrackPresent)).toInt());
  m_headerEditorDroppedFilesPolicy     = static_cast<HeaderEditorDroppedFilesPolicy>(reg.value(s_valHeaderEditorDroppedFilesPolicy, static_cast<int>(HeaderEditorDroppedFilesPolicy::Ask)).toInt());

  m_outputFileNamePolicy               = static_cast<OutputFileNamePolicy>(reg.value(s_valOutputFileNamePolicy, static_cast<int>(ToSameAsFirstInputFile)).toInt());
  m_autoDestinationOnlyForVideoFiles   = reg.value(s_valAutoDestinationOnlyForVideoFiles, false).toBool();
  m_relativeOutputDir                  = QDir{reg.value(s_valRelativeOutputDir).toString()};
  m_fixedOutputDir                     = QDir{reg.value(s_valFixedOutputDir).toString()};
  m_uniqueOutputFileNames              = reg.value(s_valUniqueOutputFileNames,   true).toBool();
  m_autoClearOutputFileName            = reg.value(s_valAutoClearOutputFileName, m_outputFileNamePolicy != DontSetOutputFileName).toBool();

  m_enableMuxingTracksByLanguage       = reg.value(s_valEnableMuxingTracksByLanguage, false).toBool();
  m_enableMuxingAllVideoTracks         = reg.value(s_valEnableMuxingAllVideoTracks, true).toBool();
  m_enableMuxingAllAudioTracks         = reg.value(s_valEnableMuxingAllAudioTracks, false).toBool();
  m_enableMuxingAllSubtitleTracks      = reg.value(s_valEnableMuxingAllSubtitleTracks, false).toBool();
  m_enableMuxingTracksByTheseLanguages = reg.value(s_valEnableMuxingTracksByTheseLanguages).toStringList();

  if (reg.contains("enableMuxingTracksByTheseTypes"))
    enableMuxingTracksByTheseTypes     = reg.value(s_valEnableMuxingTracksByTheseTypes);

  m_useDefaultJobDescription           = reg.value(s_valUseDefaultJobDescription,       false).toBool();
  m_showOutputOfAllJobs                = reg.value(s_valShowOutputOfAllJobs,            true).toBool();
  m_switchToJobOutputAfterStarting     = reg.value(s_valSwitchToJobOutputAfterStarting, false).toBool();
  m_resetJobWarningErrorCountersOnExit = reg.value(s_valResetJobWarningErrorCountersOnExit, false).toBool();
  m_jobRemovalPolicy                   = static_cast<JobRemovalPolicy>(reg.value(s_valJobRemovalPolicy, static_cast<int>(JobRemovalPolicy::Never)).toInt());
  m_removeOldJobs                      = reg.value(s_valRemoveOldJobs,                                  true).toBool();
  m_removeOldJobsDays                  = reg.value(s_valRemoveOldJobsDays,                              14).toInt();

  m_showToolSelector                   = reg.value(s_valShowToolSelector, true).toBool();
  m_warnBeforeClosingModifiedTabs      = reg.value(s_valWarnBeforeClosingModifiedTabs, true).toBool();
  m_warnBeforeAbortingJobs             = reg.value(s_valWarnBeforeAbortingJobs, true).toBool();
  m_warnBeforeOverwriting              = reg.value(s_valWarnBeforeOverwriting,  true).toBool();
  m_showMoveUpDownButtons              = reg.value(s_valShowMoveUpDownButtons, false).toBool();

  m_chapterNameTemplate                = reg.value(s_valChapterNameTemplate, QY("Chapter <NUM:2>")).toString();
  m_dropLastChapterFromBlurayPlaylist  = reg.value(s_valDropLastChapterFromBlurayPlaylist, true).toBool();
  m_ceTextFileCharacterSet             = reg.value(s_valCeTextFileCharacterSet).toString();

  m_mediaInfoExe                       = reg.value(s_valMediaInfoExe, Q("mediainfo-gui")).toString();

#if defined(HAVE_LIBINTL_H)
  m_uiLocale                           = reg.value(s_valUiLocale).toString();
#endif
  m_uiDisableHighDPIScaling            = reg.value(s_valUiDisableHighDPIScaling).toBool();
  m_uiFontFamily                       = reg.value(s_valUiFontFamily,    defaultFont.family()).toString();
  m_uiFontPointSize                    = reg.value(s_valUiFontPointSize, defaultFont.pointSize()).toInt();

  reg.beginGroup(s_grpUpdates);
  m_checkForUpdates                    = reg.value(s_valCheckForUpdates, true).toBool();
  m_lastUpdateCheck                    = reg.value(s_valLastUpdateCheck, QDateTime{}).toDateTime();

  reg.endGroup();               // settings.updates

  m_mergeLastFixedOutputDirs   .setItems(reg.value(s_valMergeLastFixedOutputDirs).toStringList());
  m_mergeLastOutputDirs        .setItems(reg.value(s_valMergeLastOutputDirs).toStringList());
  m_mergeLastRelativeOutputDirs.setItems(reg.value(s_valMergeLastRelativeOutputDirs).toStringList());

  reg.endGroup();               // settings

  loadDefaults(reg, guiVersion);
  loadDerivingTrackLanguagesSettings(reg);
  loadSplitterSizes(reg);
  loadDefaultInfoJobSettings(reg);
  loadRunProgramConfigurations(reg);
  addDefaultRunProgramConfigurations(reg);
  setDefaults(enableMuxingTracksByTheseTypes);

  mtx::chapters::g_chapter_generation_name_template.override(to_utf8(m_chapterNameTemplate));
}

void
Settings::setDefaults(boost::optional<QVariant> enableMuxingTracksByTheseTypes) {
  if (m_oftenUsedLanguages.isEmpty())
    for (auto const &languageCode : g_popular_language_codes)
      m_oftenUsedLanguages << Q(languageCode);

  if (m_oftenUsedCountries.isEmpty())
    for (auto const &countryCode : g_popular_country_codes)
      m_oftenUsedCountries << Util::mapToTopLevelCountryCode(Q(countryCode));

  if (m_oftenUsedCharacterSets.isEmpty())
    for (auto const &characterSet : g_popular_character_sets)
      m_oftenUsedCharacterSets << Q(characterSet);

  if (m_recognizedTrackLanguagesInFileNames.isEmpty())
    for (auto const &languageCode : g_popular_language_codes)
      m_recognizedTrackLanguagesInFileNames << Q(languageCode);

  if (m_regexForDerivingTrackLanguagesFromFileNames.isEmpty())
    m_regexForDerivingTrackLanguagesFromFileNames = mtx::gui::Merge::SourceFile::defaultRegexForDerivingLanguageFromFileName();

  if (ToParentOfFirstInputFile == m_outputFileNamePolicy) {
    m_outputFileNamePolicy = ToRelativeOfFirstInputFile;
    m_relativeOutputDir    = Q("..");
  }

  m_enableMuxingTracksByTheseTypes.clear();
  if (enableMuxingTracksByTheseTypes)
    for (auto const &type : enableMuxingTracksByTheseTypes->toList())
      m_enableMuxingTracksByTheseTypes << static_cast<Merge::TrackType>(type.toInt());

  else
    for (int type = static_cast<int>(Merge::TrackType::Min); type <= static_cast<int>(Merge::TrackType::Max); ++type)
      m_enableMuxingTracksByTheseTypes << static_cast<Merge::TrackType>(type);

  if (m_mergePredefinedSplitSizes.isEmpty())
    m_mergePredefinedSplitSizes
      << Q("350M")
      << Q("650M")
      << Q("700M")
      << Q("703M")
      << Q("800M")
      << Q("1000M")
      << Q("4483M")
      << Q("8142M");

  if (m_mergePredefinedSplitDurations.isEmpty())
    m_mergePredefinedSplitDurations
      << Q("01:00:00")
      << Q("1800s");
}

void
Settings::loadDefaults(QSettings &reg,
                       QString const &guiVersion) {
  reg.beginGroup(s_grpDefaults);
  m_defaultAudioTrackLanguage          = reg.value(s_valDefaultAudioTrackLanguage,    Q("und")).toString();
  m_defaultVideoTrackLanguage          = reg.value(s_valDefaultVideoTrackLanguage,    Q("und")).toString();
  m_defaultSubtitleTrackLanguage       = reg.value(s_valDefaultSubtitleTrackLanguage, Q("und")).toString();
  m_whenToSetDefaultLanguage           = static_cast<SetDefaultLanguagePolicy>(reg.value(s_valWhenToSetDefaultLanguage,     static_cast<int>(SetDefaultLanguagePolicy::IfAbsentOrUndetermined)).toInt());
  m_defaultChapterLanguage             = reg.value(s_valDefaultChapterLanguage, Q("und")).toString();
  m_defaultChapterCountry              = Util::mapToTopLevelCountryCode(reg.value(s_valDefaultChapterCountry).toString());
  auto subtitleCharset                 = reg.value(s_valDefaultSubtitleCharset).toString();
  m_defaultSubtitleCharset             = guiVersion.isEmpty() && (subtitleCharset == Q("ISO-8859-15")) ? Q("") : subtitleCharset; // Fix for a bug in versions prior to 8.2.0.
  m_defaultAdditionalMergeOptions      = reg.value(s_valDefaultAdditionalMergeOptions).toString();
  reg.endGroup();               // defaults
}

void
Settings::loadDerivingTrackLanguagesSettings(QSettings &reg) {
  reg.beginGroup(s_grpSettings);
  reg.beginGroup(s_grpDerivingTrackLanguagesFromFileNames);

  m_deriveAudioTrackLanguageFromFileNamePolicy    = static_cast<DeriveLanguageFromFileNamePolicy>(reg.value(s_valAudioPolicy,    static_cast<int>(DeriveLanguageFromFileNamePolicy::IfAbsentOrUndetermined)).toInt());
  m_deriveVideoTrackLanguageFromFileNamePolicy    = static_cast<DeriveLanguageFromFileNamePolicy>(reg.value(s_valVideoPolicy,    static_cast<int>(DeriveLanguageFromFileNamePolicy::Never)).toInt());
  m_deriveSubtitleTrackLanguageFromFileNamePolicy = static_cast<DeriveLanguageFromFileNamePolicy>(reg.value(s_valSubtitlePolicy, static_cast<int>(DeriveLanguageFromFileNamePolicy::IfAbsentOrUndetermined)).toInt());
  m_regexForDerivingTrackLanguagesFromFileNames   = reg.value(s_valCustomRegex).toString();
  m_recognizedTrackLanguagesInFileNames           = reg.value(s_valRecognizedTrackLanguagesInFileNames).toStringList();

  reg.endGroup();
  reg.endGroup();
}

void
Settings::loadSplitterSizes(QSettings &reg) {
  reg.beginGroup(s_grpSplitterSizes);

  m_splitterSizes.clear();
  for (auto const &name : reg.childKeys()) {
    auto sizes = reg.value(name).toList();
    for (auto const &size : sizes)
      m_splitterSizes[name] << size.toInt();
  }

  reg.endGroup();               // splitterSizes
}

void
Settings::loadDefaultInfoJobSettings(QSettings &reg) {
  reg.beginGroup(s_grpSettings);
  reg.beginGroup(s_grpInfo);
  reg.beginGroup(s_grpDefaultJobSettings);

  auto &s          = m_defaultInfoJobSettings;
  s.m_mode         = static_cast<Info::JobSettings::Mode>(     reg.value(s_valMode,      static_cast<int>(Info::JobSettings::Mode::Tree))                   .toInt());
  s.m_verbosity    = static_cast<Info::JobSettings::Verbosity>(reg.value(s_valVerbosity, static_cast<int>(Info::JobSettings::Verbosity::StopAtFirstCluster)).toInt());
  s.m_hexDumps     = static_cast<Info::JobSettings::HexDumps>( reg.value(s_valHexDumps,  static_cast<int>(Info::JobSettings::HexDumps::None))               .toInt());
  s.m_checksums    = reg.value(s_valChecksums).toBool();
  s.m_trackInfo    = reg.value(s_valTrackInfo).toBool();
  s.m_hexPositions = reg.value(s_valHexPositions).toBool();

  reg.endGroup();
  reg.endGroup();
  reg.endGroup();
}

void
Settings::loadRunProgramConfigurations(QSettings &reg) {
  m_runProgramConfigurations.clear();

  reg.beginGroup(s_grpRunProgramConfigurations);

  auto groups = reg.childGroups();
  groups.sort();

  for (auto const &group : groups) {
    auto cfg = std::make_shared<RunProgramConfig>();

    reg.beginGroup(group);
    cfg->m_active      = reg.value(s_valActive, true).toBool();
    cfg->m_name        = reg.value(s_valName).toString();
    auto type          = reg.value(s_valType, static_cast<int>(RunProgramType::ExecuteProgram)).toInt();
    cfg->m_type        = (type > static_cast<int>(RunProgramType::Min)) && (type < static_cast<int>(RunProgramType::Max)) ? static_cast<RunProgramType>(type) : RunProgramType::Default;
    cfg->m_forEvents   = static_cast<RunProgramForEvents>(reg.value(s_valForEvents).toInt());
    cfg->m_commandLine = reg.value(s_valCommandLine).toStringList();
    cfg->m_audioFile   = reg.value(s_valAudioFile).toString();
    cfg->m_volume      = std::min(reg.value(s_valVolume, 50).toUInt(), 100u);
    reg.endGroup();

    if (!cfg->m_active || cfg->isValid())
      m_runProgramConfigurations << cfg;
  }

  reg.endGroup();               // runProgramConfigurations
}

void
Settings::addDefaultRunProgramConfigurationForType(QSettings &reg,
                                                   RunProgramType type,
                                                   std::function<void(RunProgramConfig &)> const &modifier) {
  auto guard = Q("addedDefaultConfigurationType%1").arg(static_cast<int>(type));

  if (reg.value(guard).toBool() || !App::programRunner().isRunProgramTypeSupported(type))
    return;

  auto cfg      = std::make_shared<RunProgramConfig>();

  cfg->m_active = true;
  cfg->m_type   = type;

  if (modifier)
    modifier(*cfg);

  if (cfg->isValid()) {
    m_runProgramConfigurations << cfg;
    reg.setValue(guard, true);
  }
}

void
Settings::addDefaultRunProgramConfigurations(QSettings &reg) {
  reg.beginGroup(s_grpRunProgramConfigurations);

  auto numConfigurationsBefore = m_runProgramConfigurations.count();

  addDefaultRunProgramConfigurationForType(reg, RunProgramType::PlayAudioFile, [](RunProgramConfig &cfg) { cfg.m_audioFile = App::programRunner().defaultAudioFileName(); });
  addDefaultRunProgramConfigurationForType(reg, RunProgramType::SleepComputer);
  addDefaultRunProgramConfigurationForType(reg, RunProgramType::HibernateComputer);
  addDefaultRunProgramConfigurationForType(reg, RunProgramType::ShutDownComputer);

  auto changed = fixDefaultAudioFileNameBug();

  if ((numConfigurationsBefore != m_runProgramConfigurations.count()) || changed)
    saveRunProgramConfigurations(reg);

  reg.endGroup();               // runProgramConfigurations
}

bool
Settings::fixDefaultAudioFileNameBug() {
#if defined(SYS_WINDOWS)
  // In version v11.0.0 the default audio file name is wrong:
  // <MTX_INSTALLATION_DIRECTORY>\sounds\… instead of
  // <MTX_INSTALLATION_DIRECTORY>\data\sounds\… where the default
  // sound files are actually installed. As each configuration is only
  // added once, update an existing configuration to the actual path.
  QRegularExpression wrongFileNameRE{"<MTX_INSTALLATION_DIRECTORY>[/\\\\]sounds[/\\\\]finished-1\\.ogg"};
  auto changed = false;

  for (auto const &config : m_runProgramConfigurations) {
    if (   (config->m_type != RunProgramType::PlayAudioFile)
        || !config->m_audioFile.contains(wrongFileNameRE))
      continue;

    config->m_audioFile = App::programRunner().defaultAudioFileName();
    changed             = true;
  }

  return changed;

#else
  return false;
#endif
}

QString
Settings::actualMkvmergeExe()
  const {
  return exeWithPath(Q("mkvmerge"));
}

void
Settings::save()
  const {
  auto regPtr = registry();
  auto &reg   = *regPtr;

  QVariantList enableMuxingTracksByTheseTypes;
  for (auto type : m_enableMuxingTracksByTheseTypes)
    enableMuxingTracksByTheseTypes << static_cast<int>(type);

  reg.beginGroup(s_grpInfo);
  reg.setValue(s_valGuiVersion,                         Q(get_current_version().to_string()));
  reg.endGroup();

  reg.beginGroup(s_grpSettings);
  reg.setValue(s_valPriority,                           static_cast<int>(m_priority));
  reg.setValue(s_valProbeRangePercentage,               m_probeRangePercentage);
  reg.setValue(s_valTabPosition,                        static_cast<int>(m_tabPosition));
  reg.setValue(s_valLastOpenDir,                        m_lastOpenDir.path());
  reg.setValue(s_valLastOutputDir,                      m_lastOutputDir.path());
  reg.setValue(s_valLastConfigDir,                      m_lastConfigDir.path());

  reg.setValue(s_valOftenUsedLanguages,                 m_oftenUsedLanguages);
  reg.setValue(s_valOftenUsedCountries,                 m_oftenUsedCountries);
  reg.setValue(s_valOftenUsedCharacterSets,             m_oftenUsedCharacterSets);

  reg.setValue(s_valOftenUsedLanguagesOnly,             m_oftenUsedLanguagesOnly);
  reg.setValue(s_valOftenUsedCountriesOnly,             m_oftenUsedCountriesOnly);
  reg.setValue(s_valOftenUsedCharacterSetsOnly,         m_oftenUsedCharacterSetsOnly);

  reg.setValue(s_valScanForPlaylistsPolicy,             static_cast<int>(m_scanForPlaylistsPolicy));
  reg.setValue(s_valMinimumPlaylistDuration,            m_minimumPlaylistDuration);

  reg.setValue(s_valSetAudioDelayFromFileName,          m_setAudioDelayFromFileName);
  reg.setValue(s_valAutoSetFileTitle,                   m_autoSetFileTitle);
  reg.setValue(s_valAutoClearFileTitle,                 m_autoClearFileTitle);
  reg.setValue(s_valClearMergeSettings,                 static_cast<int>(m_clearMergeSettings));
  reg.setValue(s_valDisableCompressionForAllTrackTypes, m_disableCompressionForAllTrackTypes);
  reg.setValue(s_valDisableDefaultTrackForSubtitles,    m_disableDefaultTrackForSubtitles);
  reg.setValue(s_valMergeEnableDialogNormGainRemoval,   m_mergeEnableDialogNormGainRemoval);
  reg.setValue(s_valMergeAlwaysShowOutputFileControls,  m_mergeAlwaysShowOutputFileControls);
  reg.setValue(s_valMergePredefinedTrackNames,          m_mergePredefinedTrackNames);
  reg.setValue(s_valMergePredefinedSplitSizes,          m_mergePredefinedSplitSizes);
  reg.setValue(s_valMergePredefinedSplitDurations,      m_mergePredefinedSplitDurations);
  reg.setValue(s_valMergeLastFixedOutputDirs,           m_mergeLastFixedOutputDirs.items());
  reg.setValue(s_valMergeLastOutputDirs,                m_mergeLastOutputDirs.items());
  reg.setValue(s_valMergeLastRelativeOutputDirs,        m_mergeLastRelativeOutputDirs.items());
  reg.setValue(s_valMergeTrackPropertiesLayout,         static_cast<int>(m_mergeTrackPropertiesLayout));
  reg.setValue(s_valMergeAddingAppendingFilesPolicy,    static_cast<int>(m_mergeAddingAppendingFilesPolicy));
  reg.setValue(s_valMergeLastAddingAppendingDecision,   static_cast<int>(m_mergeLastAddingAppendingDecision));
  reg.setValue(s_valMergeWarnMissingAudioTrack,         static_cast<int>(m_mergeWarnMissingAudioTrack));
  reg.setValue(s_valHeaderEditorDroppedFilesPolicy,     static_cast<int>(m_headerEditorDroppedFilesPolicy));

  reg.setValue(s_valOutputFileNamePolicy,               static_cast<int>(m_outputFileNamePolicy));
  reg.setValue(s_valAutoDestinationOnlyForVideoFiles,   m_autoDestinationOnlyForVideoFiles);
  reg.setValue(s_valRelativeOutputDir,                  m_relativeOutputDir.path());
  reg.setValue(s_valFixedOutputDir,                     m_fixedOutputDir.path());
  reg.setValue(s_valUniqueOutputFileNames,              m_uniqueOutputFileNames);
  reg.setValue(s_valAutoClearOutputFileName,            m_autoClearOutputFileName);

  reg.setValue(s_valEnableMuxingTracksByLanguage,       m_enableMuxingTracksByLanguage);
  reg.setValue(s_valEnableMuxingAllVideoTracks,         m_enableMuxingAllVideoTracks);
  reg.setValue(s_valEnableMuxingAllAudioTracks,         m_enableMuxingAllAudioTracks);
  reg.setValue(s_valEnableMuxingAllSubtitleTracks,      m_enableMuxingAllSubtitleTracks);
  reg.setValue(s_valEnableMuxingTracksByTheseLanguages, m_enableMuxingTracksByTheseLanguages);
  reg.setValue(s_valEnableMuxingTracksByTheseTypes,     enableMuxingTracksByTheseTypes);

  reg.setValue(s_valUseDefaultJobDescription,           m_useDefaultJobDescription);
  reg.setValue(s_valShowOutputOfAllJobs,                m_showOutputOfAllJobs);
  reg.setValue(s_valSwitchToJobOutputAfterStarting,     m_switchToJobOutputAfterStarting);
  reg.setValue(s_valResetJobWarningErrorCountersOnExit, m_resetJobWarningErrorCountersOnExit);
  reg.setValue(s_valJobRemovalPolicy,                   static_cast<int>(m_jobRemovalPolicy));
  reg.setValue(s_valRemoveOldJobs,                      m_removeOldJobs);
  reg.setValue(s_valRemoveOldJobsDays,                  m_removeOldJobsDays);

  reg.setValue(s_valShowToolSelector,                   m_showToolSelector);
  reg.setValue(s_valWarnBeforeClosingModifiedTabs,      m_warnBeforeClosingModifiedTabs);
  reg.setValue(s_valWarnBeforeAbortingJobs,             m_warnBeforeAbortingJobs);
  reg.setValue(s_valWarnBeforeOverwriting,              m_warnBeforeOverwriting);
  reg.setValue(s_valShowMoveUpDownButtons,              m_showMoveUpDownButtons);

  reg.setValue(s_valChapterNameTemplate,                m_chapterNameTemplate);
  reg.setValue(s_valDropLastChapterFromBlurayPlaylist,  m_dropLastChapterFromBlurayPlaylist);
  reg.setValue(s_valCeTextFileCharacterSet,             m_ceTextFileCharacterSet);

  reg.setValue(s_valUiLocale,                           m_uiLocale);
  reg.setValue(s_valUiDisableHighDPIScaling,            m_uiDisableHighDPIScaling);
  reg.setValue(s_valUiFontFamily,                       m_uiFontFamily);
  reg.setValue(s_valUiFontPointSize,                    m_uiFontPointSize);

  reg.setValue(s_valMediaInfoExe,                       m_mediaInfoExe);

  reg.beginGroup(s_grpUpdates);
  reg.setValue(s_valCheckForUpdates,                    m_checkForUpdates);
  reg.setValue(s_valLastUpdateCheck,                    m_lastUpdateCheck);
  reg.endGroup();               // settings.updates
  reg.endGroup();               // settings

  saveDefaults(reg);
  saveDerivingTrackLanguagesSettings(reg);
  saveSplitterSizes(reg);
  saveDefaultInfoJobSettings(reg);
  saveRunProgramConfigurations(reg);
}

void
Settings::saveDefaults(QSettings &reg)
  const {
  reg.beginGroup(s_grpDefaults);
  reg.setValue(s_valDefaultAudioTrackLanguage,          m_defaultAudioTrackLanguage);
  reg.setValue(s_valDefaultVideoTrackLanguage,          m_defaultVideoTrackLanguage);
  reg.setValue(s_valDefaultSubtitleTrackLanguage,       m_defaultSubtitleTrackLanguage);
  reg.setValue(s_valWhenToSetDefaultLanguage,           static_cast<int>(m_whenToSetDefaultLanguage));
  reg.setValue(s_valDefaultChapterLanguage,             m_defaultChapterLanguage);
  reg.setValue(s_valDefaultChapterCountry,              m_defaultChapterCountry);
  reg.setValue(s_valDefaultSubtitleCharset,             m_defaultSubtitleCharset);
  reg.setValue(s_valDefaultAdditionalMergeOptions,      m_defaultAdditionalMergeOptions);
  reg.endGroup();               // defaults
}

void
Settings::saveDerivingTrackLanguagesSettings(QSettings &reg)
  const {
  reg.beginGroup(s_grpSettings);
  reg.beginGroup(s_grpDerivingTrackLanguagesFromFileNames);

  reg.setValue(s_valAudioPolicy,                         static_cast<int>(m_deriveAudioTrackLanguageFromFileNamePolicy));
  reg.setValue(s_valVideoPolicy,                         static_cast<int>(m_deriveVideoTrackLanguageFromFileNamePolicy));
  reg.setValue(s_valSubtitlePolicy,                      static_cast<int>(m_deriveSubtitleTrackLanguageFromFileNamePolicy));
  reg.setValue(s_valCustomRegex,                         m_regexForDerivingTrackLanguagesFromFileNames);
  reg.setValue(s_valRecognizedTrackLanguagesInFileNames, m_recognizedTrackLanguagesInFileNames);

  reg.endGroup();
  reg.endGroup();
}

void
Settings::saveSplitterSizes(QSettings &reg)
  const {
  reg.beginGroup(s_grpSplitterSizes);
  for (auto const &name : m_splitterSizes.keys()) {
    auto sizes = QVariantList{};
    for (auto const &size : m_splitterSizes[name])
      sizes << size;
    reg.setValue(name, sizes);
  }
  reg.endGroup();               // splitterSizes

  saveRunProgramConfigurations(reg);
}

void
Settings::saveDefaultInfoJobSettings(QSettings &reg)
  const {
  reg.beginGroup(s_grpSettings);
  reg.beginGroup(s_grpInfo);
  reg.beginGroup(s_grpDefaultJobSettings);

  auto &s = m_defaultInfoJobSettings;
  reg.setValue(s_valMode,         static_cast<int>(s.m_mode));
  reg.setValue(s_valVerbosity,    static_cast<int>(s.m_verbosity));
  reg.setValue(s_valHexDumps,     static_cast<int>(s.m_hexDumps));
  reg.setValue(s_valChecksums,    s.m_checksums);
  reg.setValue(s_valTrackInfo,    s.m_trackInfo);
  reg.setValue(s_valHexPositions, s.m_hexPositions);

  reg.endGroup();
  reg.endGroup();
  reg.endGroup();
}

void
Settings::saveRunProgramConfigurations(QSettings &reg)
  const {
  reg.beginGroup(s_grpRunProgramConfigurations);

  auto groups = reg.childGroups();
  groups.sort();

  for (auto const &group : groups)
    reg.remove(group);

  auto idx = 0;
  for (auto const &cfg : m_runProgramConfigurations) {
    reg.beginGroup(Q("%1").arg(++idx, 4, 10, Q('0')));
    reg.setValue(s_valActive,      cfg->m_active);
    reg.setValue(s_valName,        cfg->m_name);
    reg.setValue(s_valType,        static_cast<int>(cfg->m_type));
    reg.setValue(s_valForEvents,   static_cast<int>(cfg->m_forEvents));
    reg.setValue(s_valCommandLine, cfg->m_commandLine);
    reg.setValue(s_valAudioFile,   cfg->m_audioFile);
    reg.setValue(s_valVolume,      cfg->m_volume);
    reg.endGroup();
  }

  reg.endGroup();               // runProgramConfigurations
}

QString
Settings::priorityAsString()
  const {
  return LowestPriority == m_priority ? Q("lowest")
       : LowPriority    == m_priority ? Q("lower")
       : NormalPriority == m_priority ? Q("normal")
       : HighPriority   == m_priority ? Q("higher")
       :                                Q("highest");
}

QString
Settings::exeWithPath(QString const &exe) {
  auto path          = bfs::path{ to_utf8(exe) };
  auto program       = path.filename();
  auto installPath   = bfs::path{ to_utf8(App::applicationDirPath()) };
  auto potentialExes = QList<bfs::path>{} << path << (installPath / path) << (installPath / ".." / path);

#if defined(SYS_WINDOWS)
  for (auto &potentialExe : potentialExes)
    potentialExe.replace_extension(bfs::path{"exe"});

  program.replace_extension(bfs::path{"exe"});
#endif  // SYS_WINDOWS

  for (auto const &potentialExe : potentialExes)
    if (bfs::exists(potentialExe))
      return to_qs(potentialExe.string());

  auto location = QStandardPaths::findExecutable(to_qs(program.string()));
  if (!location.isEmpty())
    return location;

  return exe;
}

void
Settings::setValue(QString const &group,
                   QString const &key,
                   QVariant const &value) {
  withGroup(group, [&key, &value](QSettings &reg) {
    reg.setValue(key, value);
  });
}

QVariant
Settings::value(QString const &group,
                QString const &key,
                QVariant const &defaultValue)
  const {
  auto result = QVariant{};

  withGroup(group, [&key, &defaultValue, &result](QSettings &reg) {
    result = reg.value(key, defaultValue);
  });

  return result;
}

void
Settings::withGroup(QString const &group,
                    std::function<void(QSettings &)> worker) {
  auto reg    = registry();
  auto groups = group.split(Q("/"));

  for (auto const &subGroup : groups)
    reg->beginGroup(subGroup);

  worker(*reg);

  for (auto idx = groups.size(); idx > 0; --idx)
    reg->endGroup();
}

void
Settings::handleSplitterSizes(QSplitter *splitter) {
  restoreSplitterSizes(splitter);
  connect(splitter, &QSplitter::splitterMoved, this, &Settings::storeSplitterSizes);
}

void
Settings::restoreSplitterSizes(QSplitter *splitter) {
 auto name   = splitter->objectName();
 auto &sizes = m_splitterSizes[name];

  if (sizes.isEmpty())
    for (auto idx = 0, numWidgets = splitter->count(); idx < numWidgets; ++idx)
      sizes << 1;

  splitter->setSizes(sizes);
}

void
Settings::storeSplitterSizes() {
  auto splitter = dynamic_cast<QSplitter *>(sender());
  if (splitter)
    m_splitterSizes[ splitter->objectName() ] = splitter->sizes();
  else
    qDebug() << "storeSplitterSize() signal from non-splitter" << sender() << sender()->objectName();
}

QString
Settings::localeToUse(QString const &requestedLocale)
  const {
  auto locale = to_utf8(requestedLocale);

#if defined(HAVE_LIBINTL_H)
  translation_c::initialize_available_translations();

  if (locale.empty())
    locale = to_utf8(m_uiLocale);

  if (-1 == translation_c::look_up_translation(locale))
    locale = "";

  if (locale.empty()) {
    locale = boost::regex_replace(translation_c::get_default_ui_locale(), boost::regex{"\\..*", boost::regex::perl}, "");
    if (-1 == translation_c::look_up_translation(locale))
      locale = "";
  }
#endif

  return to_qs(locale);
}

QString
Settings::lastConfigDirPath()
  const {
  return Util::dirPath(m_lastConfigDir);
}

QString
Settings::lastOpenDirPath()
  const {
  return Util::dirPath(m_lastOpenDir);
}

void
Settings::runOncePerVersion(QString const &topic,
                            std::function<void()> worker) {
  auto reg = registry();
  auto key = Q("runOncePerVersion/%1").arg(topic);

  auto lastRunInVersion       = reg->value(key).toString();
  auto lastRunInVersionNumber = version_number_t{to_utf8(lastRunInVersion)};
  auto currentVersionNumber   = get_current_version();

  if (   lastRunInVersionNumber.valid
      && !(lastRunInVersionNumber < currentVersionNumber))
    return;

  reg->setValue(key, Q(currentVersionNumber.to_string()));

  worker();
}

}}}
