#include "activities/micromarkd/HomeWidgetView.h"

#ifdef MICROMARKD_APP

#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "activities/micromarkd/WeatherWidget.h"

namespace fui = freeink::ui;

namespace {
constexpr auto INK = fui::Paint::solid(fui::Color::Black);
constexpr auto PAPER = fui::Paint::solid(fui::Color::White);

enum class Sky : uint8_t { Sun, PartlyCloudy, Cloud, Rain, Snow };

constexpr Sky skyFor(const int code) {
  if (code == 0 || code == 1) return Sky::Sun;
  if (code == 2) return Sky::PartlyCloudy;
  if (code == 3 || code == 45 || code == 48) return Sky::Cloud;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82) || (code >= 95 && code <= 99)) return Sky::Rain;
  return Sky::Cloud;
}

static_assert(skyFor(0) == Sky::Sun && skyFor(2) == Sky::PartlyCloudy && skyFor(3) == Sky::Cloud &&
              skyFor(61) == Sky::Rain && skyFor(75) == Sky::Snow);

void sun(fui::DrawTarget& target, const int16_t x, const int16_t y) {
  target.stroke(fui::Rect{static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 10), 19, 19}, INK, 2, 10);
  constexpr int8_t rays[8][4] = {{19, 1, 19, 6}, {19, 33, 19, 38}, {1, 19, 6, 19},  {33, 19, 38, 19},
                                 {6, 6, 10, 10}, {28, 28, 32, 32}, {6, 32, 10, 28}, {28, 10, 32, 6}};
  for (const auto& ray : rays)
    target.line(fui::Point{static_cast<int16_t>(x + ray[0]), static_cast<int16_t>(y + ray[1])},
                fui::Point{static_cast<int16_t>(x + ray[2]), static_cast<int16_t>(y + ray[3])}, 2, INK);
}

void cloud(fui::DrawTarget& target, const int16_t x, const int16_t y) {
  target.fill(fui::Rect{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 18), 39, 20}, PAPER, 9);
  target.stroke(fui::Rect{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 18), 39, 20}, INK, 2, 9);
  target.fill(fui::Rect{static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 10), 23, 23}, PAPER, 12);
  target.stroke(fui::Rect{static_cast<int16_t>(x + 10), static_cast<int16_t>(y + 10), 23, 23}, INK, 2, 12);
  target.fill(fui::Rect{static_cast<int16_t>(x + 11), static_cast<int16_t>(y + 28), 22, 8}, PAPER);
}

void weatherIcon(fui::DrawTarget& target, const int16_t x, const int16_t y, const int code) {
  const Sky sky = skyFor(code);
  if (sky == Sky::Sun) {
    sun(target, x, y);
    return;
  }
  if (sky == Sky::PartlyCloudy) sun(target, static_cast<int16_t>(x - 4), static_cast<int16_t>(y - 6));
  cloud(target, x, y);
  if (sky == Sky::Rain || sky == Sky::Snow) {
    for (int16_t i = 0; i < 3; ++i) {
      const int16_t px = static_cast<int16_t>(x + 10 + i * 12);
      if (sky == Sky::Rain) {
        target.line(fui::Point{px, static_cast<int16_t>(y + 41)},
                    fui::Point{static_cast<int16_t>(px - 3), static_cast<int16_t>(y + 47)}, 2, INK);
      } else {
        target.line(fui::Point{static_cast<int16_t>(px - 3), static_cast<int16_t>(y + 44)},
                    fui::Point{static_cast<int16_t>(px + 3), static_cast<int16_t>(y + 44)}, 1, INK);
        target.line(fui::Point{px, static_cast<int16_t>(y + 41)}, fui::Point{px, static_cast<int16_t>(y + 47)}, 1, INK);
      }
    }
  }
}

void dots(fui::DrawTarget& target, const fui::Rect card, const HomeWidgetPage page) {
  const int16_t start = static_cast<int16_t>(card.x + card.width / 2 - 22);
  for (int16_t i = 0; i < 3; ++i) {
    const fui::Rect dot{static_cast<int16_t>(start + i * 18), static_cast<int16_t>(card.bottom() - 14), 8, 8};
    if (i == static_cast<int16_t>(page))
      target.fill(dot, INK, 4);
    else
      target.stroke(dot, INK, 1, 4);
  }
}

void summary(fui::DrawTarget& target, const fui::Rect card, const fui::ThemeTokens& theme,
             const WeatherWidget& weather) {
  const int16_t bodyY = static_cast<int16_t>(card.y + 58);
  if (!weather.hasWeather() || weather.forecastDays() == 0) {
    target.text(fui::Rect{static_cast<int16_t>(card.x + 16), bodyY, static_cast<int16_t>(card.width - 32), 40},
                tr(STR_MICROMARKD_WIDGET_UNAVAILABLE), theme.bodyText);
    return;
  }
  weatherIcon(target, static_cast<int16_t>(card.x + 17), static_cast<int16_t>(bodyY + 2), weather.weatherCode());
  char temperature[20];
  std::snprintf(temperature, sizeof(temperature), "%d °C", weather.temperature());
  target.text(fui::Rect{static_cast<int16_t>(card.x + 76), bodyY, 145, 33}, temperature, theme.titleText);
  char range[64];
  std::snprintf(range, sizeof(range), "%s %d°  %s %d°", tr(STR_MICROMARKD_WIDGET_MAX), weather.todayHigh(),
                tr(STR_MICROMARKD_WIDGET_MIN), weather.todayLow());
  target.text(fui::Rect{static_cast<int16_t>(card.x + 76), static_cast<int16_t>(bodyY + 31),
                        static_cast<int16_t>(card.width - 94), 25},
              range, theme.smallText);
  fui::TextStyle dateStyle = theme.smallText;
  dateStyle.align = fui::TextAlign::Right;
  const char* iso = weather.todayDate();
  char date[12];
  std::snprintf(date, sizeof(date), "%.2s.%.2s.%.2s", iso + 8, iso + 5, iso + 2);
  target.text(fui::Rect{static_cast<int16_t>(card.right() - 128), static_cast<int16_t>(bodyY + 3), 111, 25}, date,
              dateStyle);
}

void forecast(fui::DrawTarget& target, const fui::Rect card, const fui::ThemeTokens& theme,
              const WeatherWidget& weather) {
  const int days = weather.forecastDays();
  if (!weather.hasWeather() || days < 2) {
    target.text(fui::Rect{static_cast<int16_t>(card.x + 16), static_cast<int16_t>(card.y + 38),
                          static_cast<int16_t>(card.width - 32), 35},
                tr(STR_MICROMARKD_WIDGET_UNAVAILABLE), theme.bodyText);
    return;
  }
  int minimum = weather.forecastLow(0);
  int maximum = weather.forecastHigh(0);
  for (int day = 1; day < days; ++day) {
    minimum = std::min(minimum, weather.forecastLow(day));
    maximum = std::max(maximum, weather.forecastHigh(day));
  }
  const int span = std::max(1, maximum - minimum);
  const int16_t left = static_cast<int16_t>(card.x + 18);
  const int16_t right = static_cast<int16_t>(card.right() - 46);
  const int16_t top = static_cast<int16_t>(card.y + 46);
  const int16_t bottom = static_cast<int16_t>(card.bottom() - 24);
  const auto pointY = [&](const int temperature) {
    return static_cast<int16_t>(bottom - (temperature - minimum) * (bottom - top) / span);
  };
  target.line(fui::Point{left, bottom}, fui::Point{right, bottom}, 1, INK);
  int16_t previousX = left;
  int16_t previousY = pointY((weather.forecastHigh(0) + weather.forecastLow(0)) / 2);
  for (int day = 0; day < days; ++day) {
    const int16_t x = static_cast<int16_t>(left + day * (right - left) / (days - 1));
    const int16_t y = pointY((weather.forecastHigh(day) + weather.forecastLow(day)) / 2);
    if (day) target.line(fui::Point{previousX, previousY}, fui::Point{x, y}, 1, INK);
    target.line(fui::Point{x, pointY(weather.forecastHigh(day))}, fui::Point{x, pointY(weather.forecastLow(day))}, 1,
                INK);
    previousX = x;
    previousY = y;
  }
  char high[12];
  char low[12];
  std::snprintf(high, sizeof(high), "%d°", maximum);
  std::snprintf(low, sizeof(low), "%d°", minimum);
  target.text(fui::Rect{static_cast<int16_t>(right + 4), top, 36, 20}, high, theme.smallText);
  target.text(fui::Rect{static_cast<int16_t>(right + 4), static_cast<int16_t>(bottom - 8), 36, 20}, low,
              theme.smallText);
}

void lastBook(fui::DrawTarget& target, const fui::Rect card, const fui::ThemeTokens& theme, const char* title,
              const char* progress) {
  const int16_t bodyY = static_cast<int16_t>(card.y + 56);
  target.stroke(fui::Rect{static_cast<int16_t>(card.x + 17), static_cast<int16_t>(bodyY + 4), 35, 44}, INK, 2, 2);
  target.line(fui::Point{static_cast<int16_t>(card.x + 25), static_cast<int16_t>(bodyY + 5)},
              fui::Point{static_cast<int16_t>(card.x + 25), static_cast<int16_t>(bodyY + 47)}, 1, INK);
  fui::TextStyle titleStyle = theme.bodyText;
  titleStyle.maxLines = 2;
  target.text(fui::Rect{static_cast<int16_t>(card.x + 66), bodyY, static_cast<int16_t>(card.width - 84), 49}, title,
              titleStyle);
  target.text(fui::Rect{static_cast<int16_t>(card.x + 66), static_cast<int16_t>(bodyY + 47),
                        static_cast<int16_t>(card.width - 84), 22},
              progress, theme.smallText);
}
}  // namespace

void drawHomeWidget(fui::DrawTarget& target, const fui::Rect card, const fui::ThemeTokens& theme,
                    const HomeWidgetPage page, const WeatherWidget& weather, const char* bookTitle,
                    const char* bookProgress) {
  target.fill(card, PAPER, 9);
  target.stroke(card, INK, 1, 9);
  char weatherTitle[100];
#ifdef SIMULATOR
  std::snprintf(weatherTitle, sizeof(weatherTitle), "%s · %.42s · %s", tr(STR_MICROMARKD_WIDGET_WEATHER),
                weather.place().c_str(), tr(STR_MICROMARKD_WIDGET_DEMO));
#else
  std::snprintf(weatherTitle, sizeof(weatherTitle), "%s · %.64s", tr(STR_MICROMARKD_WIDGET_WEATHER),
                weather.place().c_str());
#endif
  char forecastTitle[64];
  std::snprintf(forecastTitle, sizeof(forecastTitle), tr(STR_MICROMARKD_WIDGET_FORECAST), weather.forecastDays());
  const char* title = page == HomeWidgetPage::Weather    ? weatherTitle
                      : page == HomeWidgetPage::Forecast ? forecastTitle
                                                         : tr(STR_MICROMARKD_WIDGET_LAST_BOOK);
  fui::TextStyle heading = theme.bodyText;
  heading.bold = true;
  heading.maxLines = 1;
  target.text(fui::Rect{static_cast<int16_t>(card.x + 16), static_cast<int16_t>(card.y + 6),
                        static_cast<int16_t>(card.width - 70), 27},
              title, heading);
  heading.align = fui::TextAlign::Right;
  target.text(fui::Rect{static_cast<int16_t>(card.right() - 52), static_cast<int16_t>(card.y + 6), 36, 27}, "...",
              heading);
  target.line(fui::Point{static_cast<int16_t>(card.x + 12), static_cast<int16_t>(card.y + 32)},
              fui::Point{static_cast<int16_t>(card.right() - 12), static_cast<int16_t>(card.y + 32)}, 1, INK);
  if (page == HomeWidgetPage::Weather)
    summary(target, card, theme, weather);
  else if (page == HomeWidgetPage::Forecast)
    forecast(target, card, theme, weather);
  else
    lastBook(target, card, theme, bookTitle, bookProgress);
  dots(target, card, page);
}

#endif
