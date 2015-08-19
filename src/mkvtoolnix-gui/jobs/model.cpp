#include "common/common_pch.h"

#include <QAbstractItemView>
#include <QDebug>
#include <QMutexLocker>
#include <QSettings>
#include <QTimer>

#include "common/list_utils.h"
#include "common/qt.h"
#include "common/sorting.h"
#include "mkvtoolnix-gui/jobs/model.h"
#include "mkvtoolnix-gui/jobs/mux_job.h"
#include "mkvtoolnix-gui/main_window/main_window.h"
#include "mkvtoolnix-gui/merge/mux_config.h"
#include "mkvtoolnix-gui/util/ini_config_file.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/util.h"
#include "mkvtoolnix-gui/watch_jobs/tab.h"
#include "mkvtoolnix-gui/watch_jobs/tool.h"

namespace mtx { namespace gui { namespace Jobs {

Model::Model(QObject *parent)
  : QStandardItemModel{parent}
  , m_mutex{QMutex::Recursive}
  , m_warningsIcon{Q(":/icons/16x16/dialog-warning.png")}
  , m_errorsIcon{Q(":/icons/16x16/dialog-error.png")}
  , m_started{}
  , m_dontStartJobsNow{}
  , m_running{}
  , m_queueNumDone{}
{
  retranslateUi();
}

Model::~Model() {
}

void
Model::retranslateUi() {
  QMutexLocker locked{&m_mutex};

  auto labels = QStringList{} << QY("Status") << Q("") << QY("Description") << QY("Type") << QY("Progress") << QY("Date added") << QY("Date started") << QY("Date finished");
  setHorizontalHeaderLabels(labels);

  horizontalHeaderItem(StatusIconColumn)->setIcon(QIcon{Q(":/icons/16x16/dialog-warning-grayscale.png")});

  horizontalHeaderItem(DescriptionColumn) ->setTextAlignment(Qt::AlignLeft  | Qt::AlignVCenter);
  horizontalHeaderItem(ProgressColumn)    ->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  horizontalHeaderItem(DateAddedColumn)   ->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  horizontalHeaderItem(DateStartedColumn) ->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  horizontalHeaderItem(DateFinishedColumn)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

  for (auto row = 0, numRows = rowCount(); row < numRows; ++row) {
    auto idx = index(row, 0);
    setRowText(itemsForRow(idx), *m_jobsById[ data(idx, Util::JobIdRole).value<uint64_t>() ]);
  }
}

QList<Job *>
Model::selectedJobs(QAbstractItemView *view) {
  QMutexLocker locked{&m_mutex};

  QList<Job *> jobs;
  Util::withSelectedIndexes(view, [&](QModelIndex const &idx) {
    jobs << m_jobsById[ data(idx, Util::JobIdRole).value<uint64_t>() ].get();
  });

  return jobs;
}

uint64_t
Model::idFromRow(int row)
  const {
  return item(row)->data(Util::JobIdRole).value<uint64_t>();
}

int
Model::rowFromId(uint64_t id)
  const {
  for (auto row = 0, numRows = rowCount(); row < numRows; ++row)
    if (idFromRow(row) == id)
      return row;
  return RowNotFound;
}

Job *
Model::fromId(uint64_t id)
  const {
  return m_jobsById.contains(id) ? m_jobsById[id].get() : nullptr;
}


bool
Model::hasJobs()
  const {
  return !!rowCount();
}

bool
Model::hasRunningJobs() {
  QMutexLocker locked{&m_mutex};

  for (auto const &job : m_jobsById)
    if (Job::Running == job->status())
      return true;

  return false;
}

bool
Model::isRunning()
  const {
  return m_running;
}

void
Model::setRowText(QList<QStandardItem *> const &items,
                  Job const &job)
  const {
  items.at(StatusColumn)      ->setText(Job::displayableStatus(job.status()));
  items.at(DescriptionColumn) ->setText(job.description());
  items.at(TypeColumn)        ->setText(job.displayableType());
  items.at(ProgressColumn)    ->setText(to_qs(boost::format("%1%%%") % job.progress()));
  items.at(DateAddedColumn)   ->setText(Util::displayableDate(job.dateAdded()));
  items.at(DateStartedColumn) ->setText(Util::displayableDate(job.dateStarted()));
  items.at(DateFinishedColumn)->setText(Util::displayableDate(job.dateFinished()));

  items[DescriptionColumn ]->setTextAlignment(Qt::AlignLeft  | Qt::AlignVCenter);
  items[ProgressColumn    ]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  items[DateAddedColumn   ]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  items[DateStartedColumn ]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  items[DateFinishedColumn]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

  auto numWarnings = job.numUnacknowledgedWarnings();
  auto numErrors   = job.numUnacknowledgedErrors();

  items[StatusIconColumn]->setIcon(numErrors ? m_errorsIcon : numWarnings ? m_warningsIcon : QIcon{});
}

QList<QStandardItem *>
Model::itemsForRow(QModelIndex const &idx) {
  auto rowItems = QList<QStandardItem *>{};

  for (auto column = 0; 8 > column; ++column)
    rowItems << itemFromIndex(idx.sibling(idx.row(), column));

  return rowItems;
}

QList<QStandardItem *>
Model::createRow(Job const &job)
  const {
  auto items = QList<QStandardItem *>{};
  for (auto idx = 0; idx < 8; ++idx)
    items << new QStandardItem{};
  setRowText(items, job);

  items[0]->setData(QVariant::fromValue(job.id()), Util::JobIdRole);

  return items;
}

void
Model::withSelectedJobs(QAbstractItemView *view,
                        std::function<void(Job &)> const &worker) {
  QMutexLocker locked{&m_mutex};

  auto jobs = selectedJobs(view);
  for (auto const &job : jobs)
    worker(*job);
}

void
Model::withAllJobs(std::function<void(Job &)> const &worker) {
  QMutexLocker locked{&m_mutex};

  for (auto row = 0, numRows = rowCount(); row < numRows; ++row)
    worker(*m_jobsById[idFromRow(row)]);
}

void
Model::withJob(uint64_t id,
               std::function<void(Job &)> const &worker) {
  QMutexLocker locked{&m_mutex};

  if (m_jobsById.contains(id))
    worker(*m_jobsById[id]);
}

void
Model::removeJobsIf(std::function<bool(Job const &)> predicate) {
  QMutexLocker locked{&m_mutex};

  auto toBeRemoved = QHash<Job const *, bool>{};

  for (auto row = rowCount(); 0 < row; --row) {
    auto job = m_jobsById[idFromRow(row - 1)].get();

    if (predicate(*job)) {
      job->removeQueueFile();
      m_jobsById.remove(job->id());
      toBeRemoved[job] = true;
      removeRow(row - 1);
    }
  }

  auto const keys = toBeRemoved.keys();
  for (auto const &job : keys)
    m_toBeProcessed.remove(job);

  updateProgress();
  updateJobStats();
  updateNumUnacknowledgedWarningsOrErrors();
}

void
Model::add(JobPtr const &job) {
  QMutexLocker locked{&m_mutex};

  m_jobsById[job->id()] = job;

  updateJobStats();
  updateNumUnacknowledgedWarningsOrErrors();

  if (job->isToBeProcessed()) {
    m_toBeProcessed.insert(job.get());
    updateProgress();
  }

  invisibleRootItem()->appendRow(createRow(*job));

  connect(job.get(), &Job::progressChanged,                          this, &Model::onProgressChanged);
  connect(job.get(), &Job::statusChanged,                            this, &Model::onStatusChanged);
  connect(job.get(), &Job::numUnacknowledgedWarningsOrErrorsChanged, this, &Model::onNumUnacknowledgedWarningsOrErrorsChanged);

  if (m_dontStartJobsNow)
    return;

  startNextAutoJob();
}

void
Model::onStatusChanged(uint64_t id,
                       mtx::gui::Jobs::Job::Status oldStatus,
                       mtx::gui::Jobs::Job::Status newStatus) {
  QMutexLocker locked{&m_mutex};

  auto row = rowFromId(id);
  if (row == RowNotFound)
    return;

  auto &job       = *m_jobsById[id];
  auto status     = job.status();
  auto numBefore  = m_toBeProcessed.count();

  if (job.isToBeProcessed())
    m_toBeProcessed.insert(&job);

  if (m_toBeProcessed.count() != numBefore)
    updateProgress();

  if (included_in(status, Job::PendingManual, Job::PendingAuto, Job::Running))
    job.setDateFinished(QDateTime{});

  item(row, StatusColumn)->setText(Job::displayableStatus(status));
  item(row, DateStartedColumn)->setText(Util::displayableDate(job.dateStarted()));
  item(row, DateFinishedColumn)->setText(Util::displayableDate(job.dateFinished()));

  if ((Job::Running == status) && !m_running) {
    m_running        = true;
    m_queueStartTime = QDateTime::currentDateTime();
    m_queueNumDone   = 0;

    emit queueStatusChanged(QueueStatus::Running);

  }

  if ((Job::Running == oldStatus) && (Job::Running != newStatus))
    ++m_queueNumDone;

  startNextAutoJob();

  processAutomaticJobRemoval(id, status);
}

void
Model::removeScheduledJobs() {
  QMutexLocker locked{&m_mutex};

  removeJobsIf([this](Job const &job) { return m_toBeRemoved[job.id()]; });
  m_toBeRemoved.clear();
}

void
Model::scheduleJobForRemoval(uint64_t id) {
  QMutexLocker locked{&m_mutex};

  m_toBeRemoved[id] = true;
  QTimer::singleShot(0, this, SLOT(removeScheduledJobs()));
}

void
Model::processAutomaticJobRemoval(uint64_t id,
                                  Job::Status status) {
  auto const &cfg = Util::Settings::get();
  if (cfg.m_jobRemovalPolicy == Util::Settings::JobRemovalPolicy::Never)
    return;

  bool doneOk       = Job::DoneOk       == status;
  bool doneWarnings = Job::DoneWarnings == status;
  bool done         = doneOk || doneWarnings || (Job::Failed == status) || (Job::Aborted == status);

  if (   ((cfg.m_jobRemovalPolicy == Util::Settings::JobRemovalPolicy::IfSuccessful)    && doneOk)
      || ((cfg.m_jobRemovalPolicy == Util::Settings::JobRemovalPolicy::IfWarningsFound) && (doneOk || doneWarnings))
      || ((cfg.m_jobRemovalPolicy == Util::Settings::JobRemovalPolicy::Always)          && done))
    scheduleJobForRemoval(id);
}

void
Model::onProgressChanged(uint64_t id,
                         unsigned int progress) {
  QMutexLocker locked{&m_mutex};

  auto row = rowFromId(id);
  if (row < rowCount()) {
    item(row, ProgressColumn)->setText(to_qs(boost::format("%1%%%") % progress));
    updateProgress();
  }
}

void
Model::onNumUnacknowledgedWarningsOrErrorsChanged(uint64_t id,
                                                  int,
                                                  int) {
  QMutexLocker locked{&m_mutex};

  auto row = rowFromId(id);
  if (-1 != row)
    setRowText(itemsForRow(index(row, 0)), *m_jobsById[id]);

  updateNumUnacknowledgedWarningsOrErrors();
}

void
Model::updateNumUnacknowledgedWarningsOrErrors() {
  auto numWarnings = 0;
  auto numErrors   = 0;

  for (auto const &job : m_jobsById) {
    numWarnings += job->numUnacknowledgedWarnings();
    numErrors   += job->numUnacknowledgedErrors();
  }

  emit numUnacknowledgedWarningsOrErrorsChanged(numWarnings, numErrors);
}

void
Model::startNextAutoJob() {
  if (m_dontStartJobsNow)
    return;

  QMutexLocker locked{&m_mutex};

  updateJobStats();

  if (!m_started)
    return;

  Job *toStart = nullptr;
  for (auto row = 0, numRows = rowCount(); row < numRows; ++row) {
    auto job = m_jobsById[idFromRow(row)].get();

    if (Job::Running == job->status())
      return;
    if (!toStart && (Job::PendingAuto == job->status()))
      toStart = job;
  }

  if (toStart) {
    MainWindow::watchCurrentJobTab()->connectToJob(*toStart);

    toStart->start();
    updateJobStats();
    return;
  }

  // All jobs are done. Clear total progress.
  m_toBeProcessed.clear();
  updateProgress();
  updateJobStats();

  auto wasRunning = m_running;
  m_running       = false;
  if (wasRunning)
    emit queueStatusChanged(QueueStatus::Stopped);
}

void
Model::startJobImmediately(Job &job) {
  QMutexLocker locked{&m_mutex};

  MainWindow::watchCurrentJobTab()->disconnectFromJob(job);
  MainWindow::watchJobTool()->viewOutput(job);

  job.start();
  updateJobStats();
}

void
Model::start() {
  m_started = true;
  startNextAutoJob();
}

void
Model::stop() {
  m_started = false;

  auto wasRunning = m_running;
  m_running       = false;
  if (wasRunning)
    emit queueStatusChanged(QueueStatus::Stopped);
}

void
Model::updateProgress() {
  QMutexLocker locked{&m_mutex};

  if (!(m_toBeProcessed.count() + m_queueNumDone))
    return;

  auto numRunning       = 0;
  auto runningProgress  = 0;

  for (auto const &job : m_toBeProcessed)
    if (Job::Running == job->status()) {
      ++numRunning;
      runningProgress += job->progress();
    }

  auto progress      = numRunning ? runningProgress / numRunning : 0u;
  auto totalProgress = (m_queueNumDone * 100 + runningProgress) / (m_toBeProcessed.count() + m_queueNumDone);

  emit progressChanged(progress, totalProgress);
}

void
Model::updateJobStats() {
  QMutexLocker locked{&m_mutex};

  auto numJobs = std::map<Job::Status, int>{ { Job::PendingAuto, 0 }, { Job::PendingManual, 0 }, { Job::Running, 0 }, { Job::Disabled, 0 } };

  for (auto const &job : m_jobsById) {
    auto idx = mtx::included_in(job->status(), Job::PendingAuto, Job::PendingManual, Job::Running) ? job->status() : Job::Disabled;
    ++numJobs[idx];
  }

  emit jobStatsChanged(numJobs[ Job::PendingAuto ], numJobs[ Job::PendingManual ], numJobs[ Job::Running ], numJobs[ Job::Disabled ]);
}

void
Model::convertJobQueueToSeparateIniFiles() {
  auto reg = Util::Settings::registry();

  reg->beginGroup("jobQueue");

  auto order        = reg->value("order").toStringList();
  auto numberOfJobs = reg->value("numberOfJobs", 0).toUInt();

  reg->endGroup();

  for (auto idx = 0u; idx < numberOfJobs; ++idx) {
    reg->beginGroup("jobQueue");
    reg->beginGroup(Q("job %1").arg(idx));

    try {
      Util::IniConfigFile cfg{*reg};
      auto job = Job::loadJob(cfg);
      if (!job)
        continue;

      job->saveQueueFile();
      order << job->uuid().toString();

    } catch (Merge::InvalidSettingsX &) {
    }

    while (!reg->group().isEmpty())
      reg->endGroup();
  }

  reg->beginGroup("jobQueue");
  reg->remove("numberOfJobs");
  for (auto idx = 0u; idx < numberOfJobs; ++idx)
    reg->remove(Q("job %1").arg(idx));
  reg->setValue("order", order);
  reg->endGroup();
}

void
Model::saveJobs() {
  QMutexLocker locked{&m_mutex};

  auto order = QStringList{};

  for (auto row = 0, numRows = rowCount(); row < numRows; ++row) {
    auto &job = m_jobsById[idFromRow(row)];

    job->saveQueueFile();
    order << job->uuid().toString();
  }

  auto reg = Util::Settings::registry();
  reg->beginGroup("jobQueue");
  reg->setValue("order", order);
  reg->endGroup();
}

void
Model::loadJobs() {
  QMutexLocker locked{&m_mutex};

  m_dontStartJobsNow = true;

  m_jobsById.clear();
  m_toBeProcessed.clear();
  removeRows(0, rowCount());

  auto order       = Util::Settings::registry()->value("jobQueue/order").toStringList();
  auto orderByUuid = QHash<QString, int>{};
  auto orderIdx    = 0;

  for (auto const &uuid : order)
    orderByUuid[uuid] = orderIdx++;

  auto queueLocation = Job::queueLocation();
  auto jobQueueFiles = QDir{queueLocation}.entryList(QStringList{} << Q("*.mtxcfg"), QDir::Files);
  auto loadedJobs    = QList<JobPtr>{};

  for (auto const &fileName : jobQueueFiles) {
    try {
      auto job = Job::loadJob(Q("%1/%2").arg(queueLocation).arg(fileName));
      if (!job)
        continue;

      loadedJobs << job;

      auto uuid = job->uuid().toString();
      if (!orderByUuid.contains(uuid))
        orderByUuid[uuid] = orderIdx++;

    } catch (Merge::InvalidSettingsX &) {
    }
  }

  mtx::sort::by(loadedJobs.begin(), loadedJobs.end(), [&orderByUuid](JobPtr const &job) { return orderByUuid[ job->uuid().toString() ]; });

  for (auto const &job : loadedJobs)
    add(job);

  updateProgress();

  m_dontStartJobsNow = false;
}

Qt::DropActions
Model::supportedDropActions()
  const {
  return Qt::MoveAction;
}

Qt::ItemFlags
Model::flags(QModelIndex const &index)
  const {
  auto defaultFlags = QStandardItemModel::flags(index) & ~Qt::ItemIsDropEnabled;
  return index.isValid() ? defaultFlags | Qt::ItemIsDragEnabled : defaultFlags | Qt::ItemIsDropEnabled;
}

bool
Model::canDropMimeData(QMimeData const *data,
                       Qt::DropAction action,
                       int row,
                       int,
                       QModelIndex const &parent)
  const {
  if (   !data
      || (Qt::MoveAction != action)
      || parent.isValid()
      || (0 > row))
    return false;

  return true;
}

bool
Model::dropMimeData(QMimeData const *data,
                    Qt::DropAction action,
                    int row,
                    int column,
                    QModelIndex const &parent) {
  if (!canDropMimeData(data, action, row, column, parent))
    return false;

  return QStandardItemModel::dropMimeData(data, action, row, 0, parent);
}

void
Model::acknowledgeAllWarnings() {
  QMutexLocker locked{&m_mutex};

  for (auto const &job : m_jobsById)
    job->acknowledgeWarnings();
}

void
Model::acknowledgeAllErrors() {
  QMutexLocker locked{&m_mutex};

  for (auto const &job : m_jobsById)
    job->acknowledgeErrors();
}

void
Model::acknowledgeSelectedWarnings(QAbstractItemView *view) {
  withSelectedJobs(view, [](Job &job) { job.acknowledgeWarnings(); });
}

void
Model::acknowledgeSelectedErrors(QAbstractItemView *view) {
  withSelectedJobs(view, [](Job &job) { job.acknowledgeErrors(); });
}

QDateTime
Model::queueStartTime()
  const {
  return m_queueStartTime;
}

}}}
