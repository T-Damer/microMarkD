#include "MarkdownGraphActivity.h"

#ifdef MICROMARKD_APP

#include <GfxRenderer.h>
#include <I18n.h>
#include <MarkdownCatalog.h>
#include <MarkdownDocument.h>
#include <MarkdownIndex.h>
#include <FreeInkUIIcon.h>
#include <FreeInkUIGfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

#include "HalStorage.h"
#include "MappedInputManager.h"
#include "activities/micromarkd/MarkdownCatalogStorage.h"
#include "activities/micromarkd/MarkdownIndexStorage.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/back32.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr char VAULT_ROOT[] = "/vault";
constexpr int NODE_LABEL_GAP = 5;
constexpr int CURVE_SEGMENTS = 8;
constexpr int ZOOM_CONTROL_SIZE = 28;
constexpr int ZOOM_CONTROL_GAP = 4;
constexpr int GRAPH_DIRECTIONS[8][2] = {
    {0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
};

struct GraphPoint {
  float x = 0.0F;
  float y = 0.0F;
};

using GraphPath = std::array<GraphPoint, CURVE_SEGMENTS + 1>;

uint8_t graphNodePattern(const uint32_t globalIndex, const std::string& path, const uint16_t degree) {
  // ponytail: stable four-bucket patterns avoid a RAM-heavy community detection pass; user-defined groups can replace
  // this hash when grouping becomes a persisted feature.
  uint32_t hash = 2166136261u ^ globalIndex;
  for (const char character : path) {
    hash ^= static_cast<uint8_t>(character);
    hash *= 16777619u;
  }
  hash ^= static_cast<uint32_t>(degree) * 31u;
  return static_cast<uint8_t>(hash & 0x03u);
}
}

MarkdownGraphActivity::MarkdownGraphActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string notePath)
    : Activity("MarkdownGraph", renderer, mappedInput), notePath_(std::move(notePath)) {
  nodes_.reserve(MAX_GRAPH_NODES);
}

void MarkdownGraphActivity::onEnter() {
  Activity::onEnter();
  if (notePath_.empty()) {
    vaultGraph_ = true;
    if (markdownIndexCatalogReady() && loadGraph()) {
      requestUpdate();
      return;
    }
    invalidateMarkdownIndexCatalog();
    indexing_ = true;
    indexer_.begin(VAULT_ROOT);
    requestUpdate();
    return;
  }
  loadGraph();
  requestUpdate();
}

bool MarkdownGraphActivity::loadGraph() {
  nodes_.clear();
  edgeCount_ = 0;
  page_ = 0;
  graphNodeCount_ = 0;
  selected_ = 0;
  panX_ = 0;
  panY_ = 0;
  zoomPercent_ = ZOOM_DEFAULT_PERCENT;

  if (vaultGraph_) {
    if (!markdownGraphCacheReady() && !rebuildMarkdownGraphCache()) return false;
    return loadGraphPage(0);
  }

  Node current;
  current.path = notePath_;
  current.label = micromarkd::vaultNoteDisplayName(notePath_);
  nodes_.push_back(std::move(current));

  micromarkd::MarkdownIndexRecord record;
  const std::string cachePath = micromarkd::markdownIndexCachePath(notePath_);
  if (!loadValidatedMarkdownIndexCache(cachePath, record)) return false;

  micromarkd::MarkdownCatalog catalog;
  MarkdownCatalogLoadReport report;
  // ponytail: cap note graph resolution at 256 cached notes; a larger view needs a
  // paged index instead of retaining the catalog in RAM.
  loadMarkdownCatalogFromCache(catalog, report, 256);

  for (const auto& link : record.metadata.links) {
    if (nodes_.size() >= MAX_GRAPH_NODES) break;
    const std::string target = catalog.resolveTarget(notePath_, link.target);
    if (target.empty() || target == notePath_) continue;
    const bool duplicate = std::any_of(nodes_.begin() + 1, nodes_.end(), [&target](const Node& node) {
      return node.path == target;
    });
    if (duplicate) continue;
    Node node;
    node.path = target;
    node.label = micromarkd::vaultNoteDisplayName(target);
    nodes_.push_back(std::move(node));
  }
  nodes_[0].degree = static_cast<uint8_t>(std::min<size_t>(nodes_.size() - 1, UINT8_MAX));
  for (size_t index = 1; index < nodes_.size(); index++) {
    nodes_[index].degree = 1;
    if (edgeCount_ < MAX_GRAPH_EDGES) {
      edges_[edgeCount_++] = {0, static_cast<uint8_t>(index)};
    }
  }
  for (Node& node : nodes_) node.pattern = graphNodePattern(node.globalIndex, node.path, node.degree);
  graphNodeCount_ = nodes_.size();
  return true;
}

bool MarkdownGraphActivity::loadGraphPage(const size_t page) {
  MarkdownGraphCachePage cached;
  if (!loadMarkdownGraphCachePage(page, GRAPH_PAGE_SIZE, MAX_GRAPH_NODES, cached)) return false;

  nodes_.clear();
  edgeCount_ = 0;
  page_ = page;
  graphNodeCount_ = cached.totalNodes;
  selected_ = 0;
  nodes_.reserve(MAX_GRAPH_NODES);
  for (size_t index = 0; index < cached.paths.size(); index++) {
    Node node;
    node.path = cached.paths[index];
    node.label = micromarkd::vaultNoteDisplayName(node.path);
    node.globalIndex = cached.globalIndices[index];
    node.degree = cached.degrees[index];
    node.pattern = graphNodePattern(node.globalIndex, node.path, node.degree);
    nodes_.push_back(std::move(node));
  }
  for (const auto& edge : cached.edges) {
    if (edge.from >= nodes_.size() || edge.to >= nodes_.size() || edgeCount_ >= MAX_GRAPH_EDGES) continue;
    edges_[edgeCount_++] = {edge.from, edge.to};
  }
  return cached.totalNodes == 0 || !nodes_.empty();
}

MarkdownGraphActivity::Node* MarkdownGraphActivity::selectedNode() {
  return selected_ < nodes_.size() ? &nodes_[selected_] : nullptr;
}

void MarkdownGraphActivity::openSelected() {
  Node* node = selectedNode();
  if (node != nullptr && !node->path.empty()) activityManager.goToReader(node->path);
}

void MarkdownGraphActivity::drawCurve(const Node& from, const Node& to) const {
  const int deltaX = to.x - from.x;
  const int deltaY = to.y - from.y;
  const float distance = std::max(1.0F, std::sqrt(static_cast<float>(deltaX * deltaX + deltaY * deltaY)));
  const int fromRadius = std::max(3, std::min(16, 6 + static_cast<int>(from.degree) * 2) * zoomPercent_ / 100);
  const int toRadius = std::max(3, std::min(16, 6 + static_cast<int>(to.degree) * 2) * zoomPercent_ / 100);
  const float startX = static_cast<float>(from.x) + static_cast<float>(deltaX) * fromRadius / distance;
  const float startY = static_cast<float>(from.y) + static_cast<float>(deltaY) * fromRadius / distance;
  const float endX = static_cast<float>(to.x) - static_cast<float>(deltaX) * toRadius / distance;
  const float endY = static_cast<float>(to.y) - static_cast<float>(deltaY) * toRadius / distance;
  float control1X = startX;
  float control1Y = startY;
  float control2X = endX;
  float control2Y = endY;
  if (std::abs(deltaX) >= std::abs(deltaY)) {
    const float bend = std::max(18.0F, std::abs(static_cast<float>(deltaX)) * 0.35F);
    control1X += deltaX >= 0 ? bend : -bend;
    control2X -= deltaX >= 0 ? bend : -bend;
  } else {
    const float bend = std::max(18.0F, std::abs(static_cast<float>(deltaY)) * 0.35F);
    control1Y += deltaY >= 0 ? bend : -bend;
    control2Y -= deltaY >= 0 ? bend : -bend;
  }

  const auto cubicPoint = [startX, startY, control1X, control1Y, control2X, control2Y, endX,
                           endY](const float t) {
    const float inverse = 1.0F - t;
    return GraphPoint{
        inverse * inverse * inverse * startX + 3.0F * inverse * inverse * t * control1X +
            3.0F * inverse * t * t * control2X + t * t * t * endX,
        inverse * inverse * inverse * startY + 3.0F * inverse * inverse * t * control1Y +
            3.0F * inverse * t * t * control2Y + t * t * t * endY,
    };
  };
  const auto quadraticPoint = [startX, startY, endX, endY](const GraphPoint control, const float t) {
    const float inverse = 1.0F - t;
    return GraphPoint{
        inverse * inverse * startX + 2.0F * inverse * t * control.x + t * t * endX,
        inverse * inverse * startY + 2.0F * inverse * t * control.y + t * t * endY,
    };
  };
  const auto drawPath = [this](const GraphPath& path) {
    for (size_t index = 1; index < path.size(); index++) {
      renderer.drawLine(static_cast<int>(path[index - 1].x), static_cast<int>(path[index - 1].y),
                        static_cast<int>(path[index].x), static_cast<int>(path[index].y), true);
    }
  };
  const auto pathHitsNode = [this, &from, &to](const GraphPath& path, const Node& node) {
    if (&node == &from || &node == &to) return false;
    const float nodeRadius = static_cast<float>(
        std::max(3, std::min(16, 6 + static_cast<int>(node.degree) * 2) * zoomPercent_ / 100));
    const float hitRadius = nodeRadius + 2.0F;
    const float hitRadiusSquared = hitRadius * hitRadius;
    for (size_t index = 1; index < path.size(); index++) {
      const float segmentStartX = path[index - 1].x;
      const float segmentStartY = path[index - 1].y;
      const float segmentX = path[index].x - segmentStartX;
      const float segmentY = path[index].y - segmentStartY;
      const float segmentLengthSquared = segmentX * segmentX + segmentY * segmentY;
      const float pointX = static_cast<float>(node.x) - segmentStartX;
      const float pointY = static_cast<float>(node.y) - segmentStartY;
      const float projection = segmentLengthSquared > 0.0F
                                   ? std::clamp((pointX * segmentX + pointY * segmentY) / segmentLengthSquared, 0.0F,
                                                1.0F)
                                   : 0.0F;
      const float closestX = segmentStartX + projection * segmentX;
      const float closestY = segmentStartY + projection * segmentY;
      const float distanceX = static_cast<float>(node.x) - closestX;
      const float distanceY = static_cast<float>(node.y) - closestY;
      if (distanceX * distanceX + distanceY * distanceY <= hitRadiusSquared) return true;
    }
    return false;
  };
  // ponytail: the O(edges * visible nodes * 8) scan stays bounded by the paged 32-node graph.
  const auto collisionCount = [this, &pathHitsNode](const GraphPath& path) {
    size_t collisions = 0;
    for (const Node& node : nodes_) {
      if (pathHitsNode(path, node)) collisions++;
    }
    return collisions;
  };

  GraphPath path{};
  for (int segment = 0; segment <= CURVE_SEGMENTS; segment++) {
    path[static_cast<size_t>(segment)] = cubicPoint(static_cast<float>(segment) / CURVE_SEGMENTS);
  }

  if (collisionCount(path) > 0) {
    const Node* blocker = nullptr;
    for (const Node& node : nodes_) {
      if (pathHitsNode(path, node)) {
        blocker = &node;
        break;
      }
    }
    if (blocker != nullptr) {
      const float midpointX = (startX + endX) * 0.5F;
      const float midpointY = (startY + endY) * 0.5F;
      const float normalX = -static_cast<float>(deltaY) / distance;
      const float normalY = static_cast<float>(deltaX) / distance;
      const float blockerSide = (static_cast<float>(blocker->x) - midpointX) * normalX +
                                (static_cast<float>(blocker->y) - midpointY) * normalY;
      const float preferredSide = blockerSide >= 0.0F ? -1.0F : 1.0F;
      const float blockerRadius = static_cast<float>(
          std::max(3, std::min(16, 6 + static_cast<int>(blocker->degree) * 2) * zoomPercent_ / 100));
      const float detourDistance = std::max(
          24.0F, blockerRadius + static_cast<float>(fromRadius + toRadius) * 0.5F + 8.0F);
      size_t bestCollisions = nodes_.size() + 1;
      GraphPoint bestControl{midpointX, midpointY};
      for (int attempt = 0; attempt < 4; attempt++) {
        const float side = attempt % 2 == 0 ? preferredSide : -preferredSide;
        const float offset = detourDistance * (attempt < 2 ? 2.0F : 3.5F);
        const GraphPoint control{midpointX + normalX * side * offset, midpointY + normalY * side * offset};
        for (int segment = 0; segment <= CURVE_SEGMENTS; segment++) {
          path[static_cast<size_t>(segment)] =
              quadraticPoint(control, static_cast<float>(segment) / CURVE_SEGMENTS);
        }
        const size_t collisions = collisionCount(path);
        if (collisions < bestCollisions) {
          bestCollisions = collisions;
          bestControl = control;
        }
        if (bestCollisions == 0) break;
      }
      for (int segment = 0; segment <= CURVE_SEGMENTS; segment++) {
        path[static_cast<size_t>(segment)] =
            quadraticPoint(bestControl, static_cast<float>(segment) / CURVE_SEGMENTS);
      }
    }
  }
  drawPath(path);
}

void MarkdownGraphActivity::drawNode(const Node& node, const bool selected, const int labelBottom) const {
  const int radius = std::max(3, std::min(16, 6 + static_cast<int>(node.degree) * 2) * zoomPercent_ / 100);
  const int stroke = selected ? std::max(2, radius / 2) : 1;
  // Edges are drawn first; clear the disk so the node remains the top layer even when routing is approximate.
  renderer.drawArc(radius, node.x, node.y, 1, 1, radius, false);
  renderer.drawArc(radius, node.x, node.y, 1, -1, radius, false);
  renderer.drawArc(radius, node.x, node.y, -1, 1, radius, false);
  renderer.drawArc(radius, node.x, node.y, -1, -1, radius, false);
  renderer.drawArc(radius, node.x, node.y, 1, 1, stroke, true);
  renderer.drawArc(radius, node.x, node.y, 1, -1, stroke, true);
  renderer.drawArc(radius, node.x, node.y, -1, 1, stroke, true);
  renderer.drawArc(radius, node.x, node.y, -1, -1, stroke, true);

  if (radius >= 5) {
    const int patternRadius = std::max(1, radius / 3);
    switch (node.pattern & 0x03u) {
      case 0:
        renderer.drawPixel(node.x, node.y, true);
        break;
      case 1:
        renderer.drawLine(node.x - patternRadius, node.y, node.x + patternRadius, node.y, true);
        break;
      case 2:
        renderer.drawLine(node.x, node.y - patternRadius, node.x, node.y + patternRadius, true);
        break;
      default:
        renderer.drawLine(node.x - patternRadius, node.y - patternRadius, node.x + patternRadius,
                          node.y + patternRadius, true);
        renderer.drawLine(node.x - patternRadius, node.y + patternRadius, node.x + patternRadius,
                          node.y - patternRadius, true);
        break;
    }
  }

  const int fontId = zoomPercent_ < 100 ? SMALL_FONT_ID : UI_10_FONT_ID;
  const int labelWidth = zoomPercent_ < 100 ? 64 : 108;
  const int lineHeight = renderer.getLineHeight(fontId);
  if ((zoomPercent_ < 80 && !selected && node.degree < 2) ||
      node.y + radius + NODE_LABEL_GAP + lineHeight >= labelBottom) {
    return;
  }
  const std::string label = renderer.truncatedText(fontId, node.label.c_str(), labelWidth);
  const int textWidth = renderer.getTextWidth(fontId, label.c_str());
  const int textX = node.x - textWidth / 2;
  const int textY = node.y + radius + NODE_LABEL_GAP;
  renderer.drawText(fontId, textX, textY, label.c_str(), true);
}

void MarkdownGraphActivity::loop() {
  int tapX = 0;
  int tapY = 0;
  const bool hasTap = mappedInput.hasTouch() && mappedInput.wasScreenTapped(tapX, tapY);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int headerY = safe.y + metrics.topPadding;
  const int backSize = metrics.headerHeight - 8;
  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int zoomY = contentTop + 4;
  const int zoomX = safe.x + safe.width - ZOOM_CONTROL_SIZE * 2 - ZOOM_CONTROL_GAP;
  const auto changeZoom = [this](const int delta) {
    zoomPercent_ = static_cast<uint8_t>(std::clamp(static_cast<int>(zoomPercent_) + delta,
                                                   static_cast<int>(ZOOM_MIN_PERCENT),
                                                   static_cast<int>(ZOOM_MAX_PERCENT)));
    if (Node* node = selectedNode()) {
      panX_ = static_cast<int16_t>(node->worldX);
      panY_ = static_cast<int16_t>(node->worldY);
    }
    requestUpdate();
  };
  if (hasTap && tapX >= safe.x + 4 && tapX < safe.x + 4 + backSize && tapY >= headerY + 4 &&
      tapY < headerY + 4 + backSize) {
    finish();
    return;
  }

  if (indexing_) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    if (indexer_.hasRecord()) {
      indexer_.takeRecord();
    } else if (!indexer_.complete()) {
      indexer_.step();
    }
    if (indexer_.complete() && !indexer_.hasRecord()) {
      indexing_ = false;
      loadGraph();
    }
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }
  if (hasTap) {
    if (tapY >= zoomY && tapY < zoomY + ZOOM_CONTROL_SIZE && tapX >= zoomX &&
        tapX < zoomX + ZOOM_CONTROL_SIZE * 2 + ZOOM_CONTROL_GAP) {
      changeZoom(tapX < zoomX + ZOOM_CONTROL_SIZE ? -ZOOM_STEP : ZOOM_STEP);
      return;
    }
    for (size_t index = 0; index < nodes_.size(); index++) {
      const Node& node = nodes_[index];
      const int nodeRadius = std::max(3, std::min(16, 6 + static_cast<int>(node.degree) * 2) * zoomPercent_ / 100);
      const int labelWidth = zoomPercent_ < 100 ? 64 : 108;
      const int nodeLabelHeight = renderer.getLineHeight(zoomPercent_ < 100 ? SMALL_FONT_ID : UI_10_FONT_ID);
      if (tapX < node.x - labelWidth / 2 || tapX >= node.x + labelWidth / 2 || tapY < node.y - nodeRadius ||
          tapY >= node.y + nodeRadius + NODE_LABEL_GAP + nodeLabelHeight) {
        continue;
      }
      selected_ = index;
      openSelected();
      return;
    }
  }
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenLongPress(touchX, touchY)) {
    size_t hit = nodes_.size();
    for (size_t index = 0; index < nodes_.size(); index++) {
      const Node& node = nodes_[index];
      const int radius = std::max(3, std::min(16, 6 + static_cast<int>(node.degree) * 2) * zoomPercent_ / 100);
      const int labelWidth = zoomPercent_ < 100 ? 64 : 108;
      const int nodeLabelHeight = renderer.getLineHeight(zoomPercent_ < 100 ? SMALL_FONT_ID : UI_10_FONT_ID);
      if (touchX >= node.x - labelWidth / 2 && touchX < node.x + labelWidth / 2 && touchY >= node.y - radius &&
          touchY < node.y + radius + NODE_LABEL_GAP + nodeLabelHeight) {
        hit = index;
        break;
      }
    }
    if (hit < nodes_.size()) {
      selected_ = hit;
      panX_ = static_cast<int16_t>(nodes_[hit].worldX);
      panY_ = static_cast<int16_t>(nodes_[hit].worldY);
      zoomPercent_ = static_cast<uint8_t>(std::min(static_cast<int>(ZOOM_MAX_PERCENT),
                                                    std::max(static_cast<int>(ZOOM_DEFAULT_PERCENT),
                                                             static_cast<int>(zoomPercent_) + ZOOM_STEP)));
    } else {
      zoomPercent_ = static_cast<uint8_t>(
          std::max(static_cast<int>(ZOOM_MIN_PERCENT), static_cast<int>(zoomPercent_) - ZOOM_STEP));
    }
    requestUpdate();
    return;
  }
  const auto swipe = mappedInput.wasSwipe();
  if (swipe != MappedInputManager::SwipeDir::None) {
    if (swipe == MappedInputManager::SwipeDir::Left) panX_ += PAN_STEP;
    if (swipe == MappedInputManager::SwipeDir::Right) panX_ -= PAN_STEP;
    if (swipe == MappedInputManager::SwipeDir::Up) panY_ += PAN_STEP;
    if (swipe == MappedInputManager::SwipeDir::Down) panY_ -= PAN_STEP;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Left) && !nodes_.empty()) {
    if (selected_ > 0) {
      selected_--;
      requestUpdate();
    } else if (vaultGraph_ && page_ > 0 && loadGraphPage(page_ - 1)) {
      selected_ = nodes_.empty() ? 0 : nodes_.size() - 1;
      requestUpdate();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Right) && !nodes_.empty()) {
    if (selected_ + 1 < nodes_.size()) {
      selected_++;
      requestUpdate();
    } else if (vaultGraph_ && page_ + 1 < (graphNodeCount_ + GRAPH_PAGE_SIZE - 1) / GRAPH_PAGE_SIZE &&
               loadGraphPage(page_ + 1)) {
      requestUpdate();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    changeZoom(ZOOM_STEP);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    changeZoom(-ZOOM_STEP);
  }
}

void MarkdownGraphActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int16_t leftReserve = mappedInput.hasTouch()
                                  ? static_cast<int16_t>(metrics.headerHeight + metrics.headerSidePadding)
                                  : 0;
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_MICROMARKD_GRAPH), nullptr, leftReserve);

  if (mappedInput.hasTouch()) {
    const auto spec = uiScaleSpec();
    fui::GfxRendererFrame<1> frame(renderer, spec.smallFontId, spec.bodyFontId, spec.titleFontId);
    fui::ButtonProps back;
    back.icon = fui::bitmapFromIcon(icon_arrow_left_32);
    back.iconSize = static_cast<int16_t>(metrics.headerHeight - 8);
    back.styles = fui::plainStyles();
    back.radius = 8;
    fui::button(frame.frame,
                fui::Rect{static_cast<int16_t>(safe.x + 4), static_cast<int16_t>(safe.y + metrics.topPadding + 4),
                           back.iconSize, back.iconSize},
                back);
  }

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = safe.y + safe.height - metrics.verticalSpacing;
  const int zoomY = contentTop + 4;
  const int zoomX = safe.x + safe.width - ZOOM_CONTROL_SIZE * 2 - ZOOM_CONTROL_GAP;
  const int labelBottom = contentBottom - renderer.getLineHeight(SMALL_FONT_ID) - metrics.verticalSpacing;

  if (indexing_) {
    char progress[64];
    const size_t completed = indexer_.completedNotes();
    const size_t queued = indexer_.queuedNotes();
    const bool enumerating = indexer_.phase() == MarkdownVaultIndexer::Phase::Enumerating;
    const char* progressLabel = enumerating ? tr(STR_MICROMARKD_SYNC_SCANNING) : tr(STR_MICROMARKD_SYNC_INDEXING);
    if (enumerating) {
      snprintf(progress, sizeof(progress), "%s %u", progressLabel, static_cast<unsigned>(queued));
    } else {
      snprintf(progress, sizeof(progress), "%s %u/%u", progressLabel, static_cast<unsigned>(completed),
               static_cast<unsigned>(queued));
    }
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + 96, progress, true, EpdFontFamily::REGULAR);
    if (!enumerating && queued > 0) {
      GUI.drawProgressBar(renderer, Rect{safe.x + 40, contentTop + 140, safe.width - 80, 16}, completed, queued);
    }
  } else if (!nodes_.empty()) {
    const int centerX = safe.x + safe.width / 2;
    const int centerY = contentTop + (contentBottom - contentTop) / 2;
    for (size_t index = 0; index < nodes_.size(); index++) {
      Node& node = nodes_[index];
      if (index == 0) {
        node.worldX = 0;
        node.worldY = 0;
      } else {
        const size_t layoutIndex = index - 1;
        const size_t ring = layoutIndex / 8 + 1;
        const size_t direction = layoutIndex % 8;
        node.worldX = GRAPH_DIRECTIONS[direction][0] * static_cast<int>(ring) * WORLD_RING_STEP;
        node.worldY = GRAPH_DIRECTIONS[direction][1] * static_cast<int>(ring) * WORLD_RING_STEP;
      }
      node.x = centerX + (node.worldX - panX_) * zoomPercent_ / 100;
      node.y = centerY + (node.worldY - panY_) * zoomPercent_ / 100;
    }

    for (size_t edge = 0; edge < edgeCount_; edge++) {
      const size_t from = edges_[edge].from;
      const size_t to = edges_[edge].to;
      if (from < nodes_.size() && to < nodes_.size()) drawCurve(nodes_[from], nodes_[to]);
    }
    for (size_t index = 0; index < nodes_.size(); index++) {
      drawNode(nodes_[index], selected_ == index, labelBottom);
    }
  }

  if (!indexing_ && nodes_.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, contentTop + 120, tr(STR_MICROMARKD_NO_GRAPH_LINKS), true,
                              EpdFontFamily::REGULAR);
  }

  if (!nodes_.empty()) {
    char graphStatus[32];
    const size_t pageCount = vaultGraph_ ? std::max<size_t>(1, (graphNodeCount_ + GRAPH_PAGE_SIZE - 1) / GRAPH_PAGE_SIZE) : 1;
    snprintf(graphStatus, sizeof(graphStatus), "%u/%u  p%u/%u  %u%%", static_cast<unsigned>(selected_ + 1),
             static_cast<unsigned>(nodes_.size()), static_cast<unsigned>(page_ + 1),
             static_cast<unsigned>(pageCount), static_cast<unsigned>(zoomPercent_));
    renderer.drawCenteredText(SMALL_FONT_ID, contentBottom - renderer.getLineHeight(SMALL_FONT_ID), graphStatus, true);
  }
  if (!indexing_) {
    renderer.drawRect(zoomX, zoomY, ZOOM_CONTROL_SIZE, ZOOM_CONTROL_SIZE);
    renderer.drawRect(zoomX + ZOOM_CONTROL_SIZE + ZOOM_CONTROL_GAP, zoomY, ZOOM_CONTROL_SIZE, ZOOM_CONTROL_SIZE);
    renderer.drawText(UI_12_FONT_ID, zoomX + 9, zoomY + 20, "-", true);
    renderer.drawText(UI_12_FONT_ID, zoomX + ZOOM_CONTROL_SIZE + ZOOM_CONTROL_GAP + 8, zoomY + 20, "+", true);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

#endif  // MICROMARKD_APP
