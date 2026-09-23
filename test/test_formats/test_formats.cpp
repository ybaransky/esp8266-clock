// Host tests for display_format.cpp: the format catalog and the pure renderers.
//
// The catalog tests exist because a format's identity is what gets persisted in
// config.json. Anything that lets two rows share an identity, or lets a stored
// identity resolve to the wrong row, is a silent data-corruption bug on every
// device in the field -- not something a human should be re-checking by eye.
#include <unity.h>
#include <string.h>

#include <RTClib.h>

#include "display_format.h"

namespace {

const FormatGroup kGroups[] = {kFmtGroupCountdown, kFmtGroupCountUp,
                               kFmtGroupClock};

// Small RAII-free helpers: the catalog's keys and labels live in flash, so a
// test that wants to compare them copies into a local buffer first.
struct Key {
  char text[kFormatKeyLength] = "";
  Key(FormatGroup group, uint8_t index) {
    displayFormatKey(group, index, text, sizeof(text));
  }
};

struct Label {
  char text[kFormatLabelLength] = "";
  Label(FormatGroup group, uint8_t index) {
    displayFormatLabel(group, index, text, sizeof(text));
  }
};

const char* groupName(FormatGroup group) {
  switch (group) {
    case kFmtGroupCountdown: return "countdown";
    case kFmtGroupCountUp: return "countup";
    case kFmtGroupClock: return "clock";
    default: return "?";
  }
}

// -- catalog integrity ------------------------------------------------------

void test_every_group_has_formats() {
  for (FormatGroup group : kGroups) {
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(
        0, static_cast<uint32_t>(displayFormatCount(group)), groupName(group));
  }
}

// Countdown and CountUp deliberately share one table, so they cannot drift.
void test_countdown_and_countup_share_one_table() {
  TEST_ASSERT_EQUAL_UINT8(displayFormatCount(kFmtGroupCountdown),
                          displayFormatCount(kFmtGroupCountUp));
  for (uint8_t i = 0; i < displayFormatCount(kFmtGroupCountdown); ++i) {
    TEST_ASSERT_EQUAL_STRING(Label(kFmtGroupCountdown, i).text,
                             Label(kFmtGroupCountUp, i).text);
  }
}

// A duplicate key means one of the two rows can never be selected again once
// a config storing that key is written.
void test_format_keys_are_unique_within_each_group() {
  for (FormatGroup group : kGroups) {
    const uint8_t count = displayFormatCount(group);
    for (uint8_t i = 0; i < count; ++i) {
      for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j) {
        const Key a(group, i);
        const Key b(group, j);
        char message[96];
        snprintf(message, sizeof(message), "%s: rows %u and %u share key '%s'",
                 groupName(group), i, j, a.text);
        TEST_ASSERT_FALSE_MESSAGE(strcmp(a.text, b.text) == 0, message);
      }
    }
  }
}

void test_format_keys_are_non_empty_and_bounded() {
  for (FormatGroup group : kGroups) {
    for (uint8_t i = 0; i < displayFormatCount(group); ++i) {
      const Key key(group, i);
      // Cast to a fixed width rather than using Unity's size_t variants, which
      // are not present in every Unity release.
      const uint32_t length = static_cast<uint32_t>(strlen(key.text));
      TEST_ASSERT_GREATER_THAN_UINT32(0, length);
      TEST_ASSERT_LESS_OR_EQUAL_UINT32(
          static_cast<uint32_t>(kFormatKeyLength - 1), length);
    }
  }
}

// The round trip that config load/save depends on.
void test_every_key_resolves_back_to_its_own_index() {
  for (FormatGroup group : kGroups) {
    for (uint8_t i = 0; i < displayFormatCount(group); ++i) {
      const Key key(group, i);
      uint8_t resolved = 0xFF;
      TEST_ASSERT_TRUE_MESSAGE(
          displayFormatIndexForKey(group, key.text, &resolved), key.text);
      TEST_ASSERT_EQUAL_UINT8(i, resolved);
    }
  }
}

void test_unknown_key_does_not_resolve() {
  uint8_t resolved = 0xFF;
  TEST_ASSERT_FALSE(
      displayFormatIndexForKey(kFmtGroupClock, "no-such-format", &resolved));
  TEST_ASSERT_FALSE(displayFormatIndexForKey(kFmtGroupClock, "", &resolved));
  TEST_ASSERT_FALSE(displayFormatIndexForKey(kFmtGroupClock, nullptr, &resolved));
}

// The values defaults.cpp and data/config.json ship must name real formats.
// This is the test that would have caught clockFmt = 7 claiming to be
// " YYYY | MM:DD | hh;mm" when index 7 was " DOW  | hh  H |  mm N".
void test_shipped_default_keys_resolve() {
  const struct {
    FormatGroup group;
    const char* key;
  } kShippedDefaults[] = {
      {kFmtGroupClock, "yyyy-mmdd-hhmm"},
      {kFmtGroupCountdown, "ddl-hhmm-ssu"},
      {kFmtGroupCountUp, "ddl-hhmm-ssu"},
  };
  for (const auto& d : kShippedDefaults) {
    uint8_t resolved = 0xFF;
    TEST_ASSERT_TRUE_MESSAGE(
        displayFormatIndexForKey(d.group, d.key, &resolved), d.key);
  }
}

// The default clock format must actually be the year/date/time layout its name
// claims, not merely some valid row.
void test_default_clock_key_is_the_layout_it_claims() {
  uint8_t index = 0xFF;
  TEST_ASSERT_TRUE(
      displayFormatIndexForKey(kFmtGroupClock, "yyyy-mmdd-hhmm", &index));
  TEST_ASSERT_EQUAL_STRING(" YYYY | MM:DD | hh;mm",
                           Label(kFmtGroupClock, index).text);
}

// Scheduling metadata is derived from panel shapes, so a tenths format must
// ask for the 100ms cadence and a blinking-colon format for the animation.
void test_refresh_rate_and_colon_are_derived_from_panels() {
  bool sawTenths = false;
  bool sawBlink = false;
  for (FormatGroup group : kGroups) {
    for (uint8_t i = 0; i < displayFormatCount(group); ++i) {
      const DisplayFormatInfo info = displayFormatInfo(group, i);
      const Label label(group, i);
      const bool labelHasTenths = strstr(label.text, ":u") != nullptr;
      const bool labelHasBlink = strchr(label.text, ';') != nullptr;
      TEST_ASSERT_EQUAL_MESSAGE(
          labelHasTenths ? RefreshRate::kOneTenth : RefreshRate::kOneSecond,
          info.refreshRate, label.text);
      TEST_ASSERT_EQUAL_MESSAGE(
          labelHasBlink ? ColonAnimation::kBlinking : ColonAnimation::kNone,
          info.colonAnimation, label.text);
      sawTenths = sawTenths || labelHasTenths;
      sawBlink = sawBlink || labelHasBlink;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(sawTenths, "no tenths format in catalog");
  TEST_ASSERT_TRUE_MESSAGE(sawBlink, "no blinking-colon format in catalog");
}

// An out-of-range index must fall back rather than read past the table.
void test_out_of_range_index_falls_back_to_the_first_format() {
  for (FormatGroup group : kGroups) {
    TEST_ASSERT_EQUAL_STRING(Label(group, 0).text, Label(group, 200).text);
    TEST_ASSERT_EQUAL_STRING(Key(group, 0).text, Key(group, 200).text);
  }
}

// -- counting renderer ------------------------------------------------------

uint8_t countingIndexFor(const char* key) {
  uint8_t index = 0;
  TEST_ASSERT_TRUE_MESSAGE(
      displayFormatIndexForKey(kFmtGroupCountdown, key, &index), key);
  return index;
}

void assertPanels(const DisplayFrame& frame, const char* p0, const char* p1,
                  const char* p2) {
  TEST_ASSERT_EQUAL_STRING(p0, frame.panels[0]);
  TEST_ASSERT_EQUAL_STRING(p1, frame.panels[1]);
  TEST_ASSERT_EQUAL_STRING(p2, frame.panels[2]);
}

// Panel 0 blanks when zero; panel 1 blanks only once panel 0 already has.
void test_counting_suppresses_leading_zero_panels() {
  const uint8_t index = countingIndexFor("dd-hhmm-ss");  // "   dd |  hh:mm |    ss"

  // 2 days, 3 hours, 4 minutes, 5 seconds: nothing suppressed.
  DisplayFrame frame = renderCountingFormat(
      index, 2 * 86400L + 3 * 3600L + 4 * 60L + 5L, 0);
  assertPanels(frame, "   2", " 3:04", "   5");

  // Under a day: the day panel blanks.
  frame = renderCountingFormat(index, 3 * 3600L + 4 * 60L + 5L, 0);
  assertPanels(frame, "    ", " 3:04", "   5");

  // Under an hour: both leading panels blank.
  frame = renderCountingFormat(index, 5L, 0);
  assertPanels(frame, "    ", "    ", "   5");

  // Fully zero: the last panel still renders.
  frame = renderCountingFormat(index, 0L, 0);
  assertPanels(frame, "    ", "    ", "   0");
}

// Once a panel is visible, everything right of it shows real values, zeros
// included -- suppression is leading-only.
void test_counting_does_not_suppress_interior_zeros() {
  const uint8_t index = countingIndexFor("dd-hhmm-ss");
  const DisplayFrame frame = renderCountingFormat(index, 2 * 86400L, 0);
  assertPanels(frame, "   2", " 0:00", "   0");
}

void test_counting_clamps_negative_durations_to_zero() {
  const uint8_t index = countingIndexFor("dd-hhmm-ss");
  const DisplayFrame frame = renderCountingFormat(index, -5000L, 0);
  assertPanels(frame, "    ", "    ", "   0");
}

// hhh:mm on one panel only fits through 99:59; above that the renderer must
// select the split variant with the same seconds panel.
void test_counting_overflows_combined_total_hours_to_the_split_format() {
  const uint8_t index = countingIndexFor("thhmm-ss");  // "      | hhh:mm |    ss"

  // 99 hours still fits on one panel.
  DisplayFrame frame = renderCountingFormat(index, 99L * 3600L + 30L * 60L, 0);
  assertPanels(frame, "    ", "99:30", "   0");

  // 100 hours does not: total hours and minutes split across two panels.
  frame = renderCountingFormat(index, 100L * 3600L + 30L * 60L, 0);
  assertPanels(frame, " 100", "  30", "   0");
}

// Numeric panels are right-justified across all four characters.
void test_counting_right_justifies_numeric_panels() {
  const uint8_t index = countingIndexFor("dd-hh-mm");  // "   dd |     hh |    mm"
  const DisplayFrame frame =
      renderCountingFormat(index, 7L * 86400L + 7L * 3600L + 7L * 60L, 0);
  assertPanels(frame, "   7", "   7", "   7");
}

// Left of a colon is blank-padded, not zero-padded.
void test_counting_colon_panel_blank_pads_the_left_value() {
  const uint8_t index = countingIndexFor("dd-hhmm-ss");
  const DisplayFrame frame =
      renderCountingFormat(index, 1L * 86400L + 9L * 3600L + 5L * 60L, 0);
  assertPanels(frame, "   1", " 9:05", "   0");
}

void test_counting_renders_tenths() {
  const uint8_t index = countingIndexFor("dd-hhmm-ssu");
  const DisplayFrame frame =
      renderCountingFormat(index, 1L * 86400L + 30L, 7);
  assertPanels(frame, "   1", " 0:00", "30:7");
}

// The label is dropped once a labeled value reaches three digits.
void test_counting_labeled_panel_drops_the_unit_at_three_digits() {
  const uint8_t index = countingIndexFor("ddl-hhmm-ss");  // " dd D |  hh:mm |    ss"

  DisplayFrame frame = renderCountingFormat(index, 5L * 86400L + 60L, 0);
  TEST_ASSERT_EQUAL_STRING(" 5 d", frame.panels[0]);

  frame = renderCountingFormat(index, 45L * 86400L + 60L, 0);
  TEST_ASSERT_EQUAL_STRING("45 d", frame.panels[0]);

  frame = renderCountingFormat(index, 456L * 86400L + 60L, 0);
  TEST_ASSERT_EQUAL_STRING(" 456", frame.panels[0]);
}

// -- clock renderer ---------------------------------------------------------

uint8_t clockIndexFor(const char* key) {
  uint8_t index = 0;
  TEST_ASSERT_TRUE_MESSAGE(
      displayFormatIndexForKey(kFmtGroupClock, key, &index), key);
  return index;
}

void test_clock_renders_year_month_day_and_time() {
  const uint8_t index = clockIndexFor("yyyy-mmdd-hhmm");
  const DateTime now(2026, 9, 18, 14, 5, 30);
  const DisplayFrame frame = renderClockFormat(index, now, false, 0, true);
  assertPanels(frame, "2026", " 9:18", "14:05");
}

// A blinking colon that is off renders without a separator so every digit
// stays visible.
void test_clock_blinking_colon_off_keeps_all_digits() {
  const uint8_t index = clockIndexFor("yyyy-mmdd-hhmm");
  const DateTime now(2026, 9, 18, 9, 5, 30);
  const DisplayFrame frame = renderClockFormat(index, now, false, 0, false);
  TEST_ASSERT_EQUAL_STRING(" 905", frame.panels[2]);
}

void test_clock_12_hour_conversion() {
  const uint8_t index = clockIndexFor("yyyy-mmdd-hhmm");
  const DateTime afternoon(2026, 9, 18, 14, 5, 0);
  TEST_ASSERT_EQUAL_STRING(
      " 2:05", renderClockFormat(index, afternoon, true, 0, true).panels[2]);

  // Midnight becomes 12, not 0.
  const DateTime midnight(2026, 9, 18, 0, 5, 0);
  TEST_ASSERT_EQUAL_STRING(
      "12:05", renderClockFormat(index, midnight, true, 0, true).panels[2]);

  const DateTime noon(2026, 9, 18, 12, 5, 0);
  TEST_ASSERT_EQUAL_STRING(
      "12:05", renderClockFormat(index, noon, true, 0, true).panels[2]);
}

// 12-hour mode is a clock-only presentation; counting formats are unaffected
// because they never receive the flag.
void test_clock_24_hour_is_the_default_rendering() {
  const uint8_t index = clockIndexFor("yyyy-mmdd-hhmm");
  const DateTime evening(2026, 9, 18, 23, 59, 0);
  TEST_ASSERT_EQUAL_STRING(
      "23:59", renderClockFormat(index, evening, false, 0, true).panels[2]);
}

void test_clock_day_of_week_uses_seven_segment_safe_forms() {
  const uint8_t index = clockIndexFor("dow-mmdd-hhmm");
  const char* kExpected[] = {" Sun", "NNon", "  tu", "UUEd", " thu", " Fri", " Sat"};
  for (uint8_t dow = 0; dow < 7; ++dow) {
    // 2026-09-13 is a Sunday; walk the week from there.
    const DateTime day(2026, 9, static_cast<uint8_t>(13 + dow), 12, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(dow, day.dayOfTheWeek());
    const DisplayFrame frame = renderClockFormat(index, day, false, 0, true);
    TEST_ASSERT_EQUAL_STRING(kExpected[dow], frame.panels[0]);
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_every_group_has_formats);
  RUN_TEST(test_countdown_and_countup_share_one_table);
  RUN_TEST(test_format_keys_are_unique_within_each_group);
  RUN_TEST(test_format_keys_are_non_empty_and_bounded);
  RUN_TEST(test_every_key_resolves_back_to_its_own_index);
  RUN_TEST(test_unknown_key_does_not_resolve);
  RUN_TEST(test_shipped_default_keys_resolve);
  RUN_TEST(test_default_clock_key_is_the_layout_it_claims);
  RUN_TEST(test_refresh_rate_and_colon_are_derived_from_panels);
  RUN_TEST(test_out_of_range_index_falls_back_to_the_first_format);
  RUN_TEST(test_counting_suppresses_leading_zero_panels);
  RUN_TEST(test_counting_does_not_suppress_interior_zeros);
  RUN_TEST(test_counting_clamps_negative_durations_to_zero);
  RUN_TEST(test_counting_overflows_combined_total_hours_to_the_split_format);
  RUN_TEST(test_counting_right_justifies_numeric_panels);
  RUN_TEST(test_counting_colon_panel_blank_pads_the_left_value);
  RUN_TEST(test_counting_renders_tenths);
  RUN_TEST(test_counting_labeled_panel_drops_the_unit_at_three_digits);
  RUN_TEST(test_clock_renders_year_month_day_and_time);
  RUN_TEST(test_clock_blinking_colon_off_keeps_all_digits);
  RUN_TEST(test_clock_12_hour_conversion);
  RUN_TEST(test_clock_24_hour_is_the_default_rendering);
  RUN_TEST(test_clock_day_of_week_uses_seven_segment_safe_forms);
  return UNITY_END();
}
