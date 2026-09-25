#include "shell/bar/widgets/custom_button_widget.h"

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/file_utils.h"
#include "util/string_utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <utility>

CustomButtonWidget::CustomButtonWidget(Options options)
    : m_glyphName(StringUtils::trim(options.glyph)), m_labelText(StringUtils::trim(options.label)),
      m_tooltip(StringUtils::trim(options.tooltip)), m_exec(StringUtils::trim(options.exec)),
      m_interval(options.interval),
      m_customImage(widget_custom_image::fromConfig(options.customImage, options.customImageColorize)) {}

void CustomButtonWidget::create() {
  auto area = ui::inputArea({});

  if (!m_tooltip.empty()) {
    area->setTooltip(m_tooltip);
  }

  if (m_customImage.enabled()) {
    area->addChild(ui::image({.out = &m_image, .fit = ImageFit::Contain}));
  } else {
    area->addChild(
        ui::glyph({
            .out = &m_glyph,
            .glyph = m_glyphName,
            .glyphSize = Style::baseGlyphSize * m_contentScale,
            .color = widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface)),
            .visible = !m_glyphName.empty(),
        })
    );
  }

  area->addChild(
      ui::label({
          .out = &m_label,
          .text = m_labelText,
          .fontSize = Style::fontSizeBody * fontScale(),
          .fontWeight = labelFontWeight(),
          .fontFamily = labelFontFamily(),
          .color = widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface)),
          .maxLines = 1,
          .visible = !m_labelText.empty(),
      })
  );

  m_area = area.get();
  setRoot(std::move(area));
  startPolling();
}

void CustomButtonWidget::doLayout(Renderer& renderer, float containerWidth, float containerHeight) {
  if (m_area == nullptr || m_label == nullptr) {
    return;
  }

  const bool isVertical = containerHeight > containerWidth;
  const bool showImage = m_image != nullptr;
  const bool showGlyph = !showImage && m_glyph != nullptr && !m_glyphName.empty();
  const bool showIcon = showImage || showGlyph;
  const bool showLabel = !m_labelText.empty();
  const float spacing = (showIcon && showLabel) ? Style::spaceXs * m_contentScale : 0.0F;

  if (m_glyph != nullptr) {
    m_glyph->setVisible(showGlyph);
  }
  m_label->setVisible(showLabel);

  if (showImage) {
    widget_custom_image::sync(
        *m_image, renderer, m_customImage, m_contentScale, widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface))
    );
  } else if (showGlyph) {
    m_glyph->setGlyphSize(Style::baseGlyphSize * m_contentScale);
    m_glyph->setColor(widgetIconColorOr(colorSpecFromRole(ColorRole::OnSurface)));
    m_glyph->measure(renderer);
  }

  if (showLabel) {
    m_label->setFontSize((isVertical ? Style::fontSizeCaption : Style::fontSizeBody) * fontScale());
    m_label->setFontWeight(labelFontWeight());
    m_label->setTextAlign(isVertical ? TextAlign::Center : TextAlign::Start);
    m_label->setMaxWidth(isVertical ? containerWidth : 0.0F);
    m_label->setColor(widgetForegroundOr(colorSpecFromRole(ColorRole::OnSurface)));
    m_label->measure(renderer);
  }

  if (isVertical) {
    float width = 0.0F;
    float height = 0.0F;
    if (showImage) {
      width = std::max(width, m_image->width());
      height += m_image->height();
    } else if (showGlyph) {
      width = std::max(width, m_glyph->width());
      height += m_glyph->height();
    }
    if (showLabel) {
      if (height > 0.0F) {
        height += spacing;
      }
      width = std::max(width, m_label->width());
      height += m_label->height();
    }

    float y = 0.0F;
    if (showImage) {
      m_image->setPosition(std::round((width - m_image->width()) * 0.5F), y);
      y += m_image->height() + spacing;
    } else if (showGlyph) {
      m_glyph->setPosition(std::round((width - m_glyph->width()) * 0.5F), y);
      y += m_glyph->height() + spacing;
    }
    if (showLabel) {
      m_label->setPosition(std::round((width - m_label->width()) * 0.5F), y);
    }
    m_area->setSize(width, height);
    return;
  }

  float width = 0.0F;
  float height = 0.0F;
  if (showImage) {
    width += m_image->width();
    height = std::max(height, m_image->height());
  } else if (showGlyph) {
    width += m_glyph->width();
    height = std::max(height, m_glyph->height());
  }
  if (showLabel) {
    if (width > 0.0F) {
      width += spacing;
    }
    width += m_label->width();
    height = std::max(height, m_label->height());
  }

  float x = 0.0F;
  if (showImage) {
    m_image->setPosition(x, std::round((height - m_image->height()) * 0.5F));
    x += m_image->width() + spacing;
  } else if (showGlyph) {
    m_glyph->setPosition(x, std::round((height - m_glyph->height()) * 0.5F));
    x += m_glyph->width() + spacing;
  }
  if (showLabel) {
    m_label->setPosition(x, std::round((height - m_label->height()) * 0.5F));
  }
  m_area->setSize(width, height);
}

void CustomButtonWidget::startPolling() {
  if (m_exec.empty()) {
    return;
  }

  executeCommand();

  if (m_interval > 0) {
    m_pollingTimer.startRepeating(std::chrono::milliseconds(m_interval), [this]() { executeCommand(); });
  }
}

void CustomButtonWidget::executeCommand() {
  if (m_running.exchange(true) || m_exec.empty()) {
    return;
  }

  const std::weak_ptr<void> aliveGuard = m_aliveGuard;

  const bool launched = process::runAsync(
      m_exec,
      {
          .onExit =
              [this, aliveGuard](process::RunResult res) {
                m_running = false;

                // Hand the output back to the main thread
                DeferredCall::callLater([this, aliveGuard, out = std::move(res.out)]() {
                  if (aliveGuard.expired()) {
                    return;
                  }
                  handleCommandOutput(out);
                });
              },
      },
      {.timeout = std::chrono::seconds(10)}
  );

  if (!launched) {
    m_running = false;
  }
}

void CustomButtonWidget::handleCommandOutput(std::string output) {
  std::string trimmed = StringUtils::trim(output);

  if (trimmed.starts_with('{')) {
    try {
      const auto json = nlohmann::json::parse(trimmed);
      if (json.is_object()) {
        if (json.contains("text") && json["text"].is_string()) {
          m_labelText = json["text"].get<std::string>();
        } else if (json.contains("label") && json["label"].is_string()) {
          m_labelText = json["label"].get<std::string>();
        }
        if (json.contains("glyph") && json["glyph"].is_string()) {
          m_glyphName = json["glyph"].get<std::string>();
        }
        if (json.contains("tooltip") && json["tooltip"].is_string()) {
          m_tooltip = json["tooltip"].get<std::string>();
        }
        if (m_image != nullptr && json.contains("image") && json["image"].is_string()) {
          m_customImage.path = FileUtils::expandUserPath(json["image"].get<std::string>()).string();
        }
      }
    } catch (const nlohmann::json::exception&) {
      m_labelText = trimmed;
    }
  } else {
    // plain text mode
    m_labelText = trimmed;
  }

  if (m_label != nullptr) {
    m_label->setText(m_labelText);
  }
  if (m_glyph != nullptr) {
    m_glyph->setGlyph(m_glyphName);
    m_glyph->setVisible(!m_glyphName.empty());
  }
  if (m_area != nullptr) {
    m_area->setTooltip(m_tooltip);
  }

  requestUpdate();
}
