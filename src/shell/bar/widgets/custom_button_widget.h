#pragma once

#include "core/timer_manager.h"
#include "shell/bar/widget.h"
#include "shell/bar/widget_custom_image.h"

#include <atomic>
#include <memory>
#include <string>

class Glyph;
class Image;
class InputArea;
class Label;

class CustomButtonWidget : public Widget {
public:
  struct Options {
    std::string glyph = "heart";
    std::string customImage;
    bool customImageColorize = false;
    std::string label;
    std::string exec;
    int interval = 0;
    std::string tooltip;
  };

  explicit CustomButtonWidget(Options options);

  void create() override;

private:
  void doLayout(Renderer& renderer, float containerWidth, float containerHeight) override;
  void startPolling();
  void executeCommand();
  void handleCommandOutput(std::string output);

  std::string m_glyphName;
  std::string m_labelText;
  std::string m_tooltip;
  std::string m_exec;
  int m_interval = 0;
  std::shared_ptr<void> m_aliveGuard = std::make_shared<int>(0);
  std::atomic<bool> m_running = false;
  Timer m_pollingTimer;
  WidgetCustomImage m_customImage;
  InputArea* m_area = nullptr;
  Glyph* m_glyph = nullptr;
  Image* m_image = nullptr;
  Label* m_label = nullptr;
};
