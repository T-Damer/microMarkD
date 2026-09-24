#pragma once

#ifdef MICROMARKD_APP

#include <FreeInkUICore.h>

#include <cstdint>

class WeatherWidget;

enum class HomeWidgetPage : uint8_t { Weather, Forecast, LastBook };

void drawHomeWidget(freeink::ui::DrawTarget& target, freeink::ui::Rect rect,
                    const freeink::ui::ThemeTokens& theme, HomeWidgetPage page, const WeatherWidget& weather,
                    const char* bookTitle, const char* bookProgress);

#endif
