#pragma once

#ifdef MICROMARKD_APP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "activities/micromarkd/MarkdownVaultIndexer.h"

class MarkdownGraphActivity final : public Activity {
 public:
  MarkdownGraphActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string notePath);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Node {
    std::string path;
    std::string label;
    uint32_t globalIndex = 0;
    int worldX = 0;
    int worldY = 0;
    int x = 0;
    int y = 0;
    uint16_t degree = 0;
    uint8_t pattern = 0;
  };

  struct Edge {
    uint8_t from = 0;
    uint8_t to = 0;
  };

  // ponytail: keep the in-RAM graph bounded at 32 notes; SD-backed graph paging can raise this later.
  static constexpr size_t MAX_GRAPH_NODES = 32;
  static constexpr size_t GRAPH_PAGE_SIZE = 24;
  static constexpr size_t MAX_GRAPH_EDGES = 128;
  static constexpr uint8_t ZOOM_MIN_PERCENT = 45;
  static constexpr uint8_t ZOOM_DEFAULT_PERCENT = 60;
  static constexpr uint8_t ZOOM_MAX_PERCENT = 180;
  static constexpr int WORLD_RING_STEP = 92;
  static constexpr int PAN_STEP = 96;
  static constexpr int ZOOM_STEP = 20;

  std::string notePath_;
  std::vector<Node> nodes_;
  std::array<Edge, MAX_GRAPH_EDGES> edges_{};
  size_t edgeCount_ = 0;
  size_t page_ = 0;
  size_t graphNodeCount_ = 0;
  size_t selected_ = 0;
  int16_t panX_ = 0;
  int16_t panY_ = 0;
  uint8_t zoomPercent_ = ZOOM_DEFAULT_PERCENT;
  bool vaultGraph_ = false;
  bool indexing_ = false;
  MarkdownVaultIndexer indexer_;

  bool loadGraph();
  bool loadGraphPage(size_t page);
  Node* selectedNode();
  void openSelected();
  void drawCurve(const Node& from, const Node& to) const;
  void drawNode(const Node& node, bool selected, int labelBottom) const;
};

#endif  // MICROMARKD_APP
