#include "ActivityManager.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cassert>
#include <memory>
#include <utility>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/boot_sleep/BootActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/home/HomeActivity.h"
#include "activities/reader/EpubReaderActivity.h"
#include "activities/reader/ImageReaderActivity.h"
#include "activities/reader/TxtReaderActivity.h"
#include "activities/reader/XtcReaderActivity.h"
#ifdef MICROMARKD_APP
#include "activities/micromarkd/MicroMarkDActivity.h"
#endif
#include "activities/settings/SettingsActivity.h"
#include "components/UiAppHost.h"

static const char* TAG = "ActivityManager";

ActivityManager activityManager;

ActivityManager::ActivityManager() {
  renderingMutex = xSemaphoreCreateRecursiveMutex();
  assert(renderingMutex != nullptr);
}

void ActivityManager::setup(GfxRenderer* renderer, MappedInputManager* mappedInput) {
  this->renderer = renderer;
  this->mappedInput = mappedInput;
}

void ActivityManager::start() {
  assert(renderer != nullptr);
  assert(mappedInput != nullptr);
  assert(activityStack.empty());

  pushActivity(std::make_unique<BootActivity>(*renderer, *mappedInput));
}

void ActivityManager::loop() {
  if (activityStack.empty()) return;

  Activity* current = activityStack.back().activity.get();
  current->loop();

  if (current != activityStack.back().activity.get()) return;

  if (current->isFinished()) {
    finishTopActivity();
  }
}

void ActivityManager::handleInput() {
  if (activityStack.empty()) return;

  Activity* current = activityStack.back().activity.get();
  current->handleInput();
}

void ActivityManager::render() {
  if (activityStack.empty()) return;

  Activity* current = activityStack.back().activity.get();
  current->render(RenderLock(*current));
}

void ActivityManager::pushActivity(std::unique_ptr<Activity> activity) {
  assert(activity != nullptr);
  assert(renderer != nullptr);
  assert(mappedInput != nullptr);

  if (!activityStack.empty()) activityStack.back().activity->onPause();

  ActivityStackEntry entry;
  entry.activity = std::move(activity);
  activityStack.push_back(std::move(entry));
  activityStack.back().activity->onEnter();
  requestUpdate();
}

void ActivityManager::startActivityForResult(std::unique_ptr<Activity> activity, ActivityResultCallback callback) {
  assert(activity != nullptr);
  assert(callback != nullptr);
  assert(renderer != nullptr);
  assert(mappedInput != nullptr);

  if (!activityStack.empty()) activityStack.back().activity->onPause();

  ActivityStackEntry entry;
  entry.activity = std::move(activity);
  entry.callback = std::move(callback);
  activityStack.push_back(std::move(entry));
  activityStack.back().activity->onEnter();
  requestUpdate();
}

void ActivityManager::finishTopActivity() {
  if (activityStack.empty()) return;

  ActivityStackEntry entry = std::move(activityStack.back());
  activityStack.pop_back();
  entry.activity->onExit();

  if (entry.callback) entry.callback(entry.activity->getResult());

  if (!activityStack.empty()) {
    activityStack.back().activity->onResume();
    requestUpdate();
  }
}

void ActivityManager::finishAllActivities() {
  while (!activityStack.empty()) {
    ActivityStackEntry entry = std::move(activityStack.back());
    activityStack.pop_back();
    entry.activity->onExit();
  }
}

void ActivityManager::goHome() {
  finishAllActivities();
#ifdef MICROMARKD_APP
  pushActivity(std::make_unique<MicroMarkDActivity>(*renderer, *mappedInput));
#else
  pushActivity(std::make_unique<HomeActivity>(*renderer, *mappedInput));
#endif
}

void ActivityManager::goToFileBrowser(const std::string& initialPath) {
  pushActivity(std::make_unique<FileBrowserActivity>(*renderer, *mappedInput, initialPath));
}

void ActivityManager::goToReader(const std::string& path) {
  if (path.empty()) return;

  if (FsHelpers::hasEpubExtension(path)) {
    pushActivity(std::make_unique<EpubReaderActivity>(*renderer, *mappedInput, path));
  } else if (FsHelpers::hasXtcExtension(path)) {
    pushActivity(std::make_unique<XtcReaderActivity>(*renderer, *mappedInput, path));
  } else if (FsHelpers::hasImageExtension(path)) {
    pushActivity(std::make_unique<ImageReaderActivity>(*renderer, *mappedInput, path));
  } else if (FsHelpers::hasTxtExtension(path) || FsHelpers::hasMarkdownExtension(path)) {
    pushActivity(std::make_unique<TxtReaderActivity>(*renderer, *mappedInput, path));
  } else {
    LOG_ERR(TAG, "Unsupported reader file: %s", path.c_str());
  }
}

void ActivityManager::goToSettings() { pushActivity(std::make_unique<SettingsActivity>(*renderer, *mappedInput)); }

void ActivityManager::onHomePressed() {
  if (activityStack.empty()) return;

  // Ignore Home while already at an activity explicitly marked as Home.
  if (activityStack.back().activity->isHomeActivity()) return;

  goHome();
}

void ActivityManager::requestUpdate(const bool force) {
  if (activityStack.empty()) return;

  activityStack.back().activity->requestUpdate(force);
}

bool ActivityManager::needsUpdate() const {
  if (activityStack.empty()) return false;
  return activityStack.back().activity->needsUpdate();
}

bool ActivityManager::needsForceUpdate() const {
  if (activityStack.empty()) return false;
  return activityStack.back().activity->needsForceUpdate();
}

void ActivityManager::clearUpdateRequest() {
  if (activityStack.empty()) return;
  activityStack.back().activity->clearUpdateRequest();
}

void ActivityManager::setRenderTaskHandle(TaskHandle_t handle) { renderTaskHandle = handle; }

TaskHandle_t ActivityManager::getRenderTaskHandle() const { return renderTaskHandle; }

void ActivityManager::notifyRenderComplete() {
  TaskHandle_t waiting = nullptr;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  waiting = waitingTaskHandle;
  waitingTaskHandle = nullptr;
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  if (waiting != nullptr) xTaskNotifyGive(waiting);
}

void ActivityManager::requestUpdateAndWait() {
  requestUpdate();

  if (renderTaskHandle == nullptr) return;

  const TaskHandle_t currTaskHandler = xTaskGetCurrentTaskHandle();
  bool isRenderTask = false;
  bool alreadyWaiting = false;
  bool holdingRenderLock = false;
  taskENTER_CRITICAL(&activityManagerSpinlock);
  isRenderTask = (currTaskHandler == renderTaskHandle);
  alreadyWaiting = (waitingTaskHandle != nullptr);
  holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
#ifndef SIMULATOR
  // Tell the power manager the loop is parked here: it cannot poll input until the
  // render finishes, so the BUSY-wait slice hook should not yield to it meanwhile.
  powerManager.noteRenderWaitBegin();
#endif
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifndef SIMULATOR
  powerManager.noteRenderWaitEnd();
#endif
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) xSemaphoreGive(activityManager.renderingMutex);
}

RenderLock::RenderLock(RenderLock&& other) noexcept {
  isLocked = other.isLocked;
  other.isLocked = false;
}

RenderLock& RenderLock::operator=(RenderLock&& other) noexcept {
  if (this != &other) {
    if (isLocked) xSemaphoreGive(activityManager.renderingMutex);
    isLocked = other.isLocked;
    other.isLocked = false;
  }
  return *this;
}
