// Screenshot + Telemetry Helper for ESP32 ST7701S
// Reads PSRAM framebuffer, RLE-encodes, sends over UART.
// Also provides render benchmarking and diagnostic commands.
//
// Commands (single byte over UART):
//   'S' — capture screenshot and send
//   'I' — send system info (heap, PSRAM, uptime)
//   'B' — run render benchmark (force full redraw + timing)
//   'F' — toggle fullscreen (enter on first tile / exit)
//   'P' — switch to next page
//   'R' — automated scene run: grid→screenshot→fullscreen→screenshot→exit→screenshot
#pragma once
#ifdef USE_ESP32_VARIANT_ESP32S3

#include "esphome/components/st7701s/st7701s.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include <cstring>

namespace screenshot_helper {

static constexpr uart_port_t UART_PORT = UART_NUM_0;
static constexpr int UART_RX_BUF = 256;
static bool uart_initialized_ = false;

// ── Wire Protocol ────────────────────────────────────────────────────
//  Screenshot:
//    START:   \x89 S C R \x02              (5 bytes, version 2)
//    HEADER:  width_le16 height_le16 payload_size_le32 crc32_le32 (12 bytes)
//    PAYLOAD: repeated [count_le16, color_le16]  (4 bytes per RLE pair)
//    END:     \x89 E N D                   (4 bytes)
//
//  Text Response (info/benchmark):
//    \x89 T X T \x01  (5 bytes)
//    ... UTF-8 text ...
//    \x89 E N D       (4 bytes)

// ── CRC32 (zlib polynomial, no lookup table) ─────────────────────────
static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return ~crc;
}

// ── UART Init ────────────────────────────────────────────────────────
// ESPHome logger uses IDF vprintf → TX works, but RX needs driver.
static void ensure_uart() {
  if (uart_initialized_) return;
  if (uart_is_driver_installed(UART_PORT)) {
    uart_initialized_ = true;
    return;
  }
  esp_err_t err = uart_driver_install(UART_PORT, UART_RX_BUF, 0, 0, nullptr, 0);
  if (err == ESP_OK) {
    uart_initialized_ = true;
    ESP_LOGI("screenshot", "UART%d RX driver installed", UART_PORT);
  } else {
    ESP_LOGE("screenshot", "UART driver install failed: %s", esp_err_to_name(err));
  }
}

// ── Command Check ────────────────────────────────────────────────────
static char check_command() {
  ensure_uart();
  if (!uart_initialized_) return 0;
  uint8_t buf[32];
  int len = uart_read_bytes(UART_PORT, buf, sizeof(buf), 0);
  for (int i = 0; i < len; i++) {
    char c = buf[i];
    if (c == 'S' || c == 'I' || c == 'B' || c == 'F' || c == 'P' || c == 'R') return c;
  }
  return 0;
}

// ── Logging Suppression ─────────────────────────────────────────────
static void suppress_logging() {
  esp_log_level_set("*", ESP_LOG_NONE);
  vTaskDelay(pdMS_TO_TICKS(30));  // drain TX ring buffer
}

static void restore_logging(esp_log_level_t level = ESP_LOG_DEBUG) {
  esp_log_level_set("*", level);
}

// ── Send Text Response ──────────────────────────────────────────────
static void send_text(const char *text) {
  suppress_logging();
  const uint8_t start[] = {0x89, 'T', 'X', 'T', 0x01};
  const uint8_t end[] = {0x89, 'E', 'N', 'D'};
  uart_write_bytes(UART_PORT, start, sizeof(start));
  uart_write_bytes(UART_PORT, text, strlen(text));
  uart_write_bytes(UART_PORT, end, sizeof(end));
  uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(5000));
  restore_logging();
}

// ── System Info ─────────────────────────────────────────────────────
static void send_system_info() {
  char buf[512];
  snprintf(buf, sizeof(buf),
    "=== System Info ===\n"
    "Uptime: %lu ms\n"
    "Free heap: %lu bytes\n"
    "Min free heap: %lu bytes\n"
    "Free PSRAM: %lu / %lu bytes\n"
    "Largest PSRAM block: %lu bytes\n",
    (unsigned long) millis(),
    (unsigned long) esp_get_free_heap_size(),
    (unsigned long) esp_get_minimum_free_heap_size(),
    (unsigned long) heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
    (unsigned long) heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
    (unsigned long) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  ESP_LOGI("screenshot", "%s", buf);
  send_text(buf);
}

// ── Framebuffer Access ──────────────────────────────────────────────
struct ST7701SAccess : esphome::st7701s::ST7701S {
  esp_lcd_panel_handle_t panel_handle() { return this->handle_; }
};

static void *get_framebuffer(esphome::display::Display *display) {
  auto *st = static_cast<esphome::st7701s::ST7701S *>(display);
  auto *access = static_cast<ST7701SAccess *>(st);
  auto handle = access->panel_handle();
  if (!handle) {
    ESP_LOGE("screenshot", "No panel handle");
    return nullptr;
  }
  void *fb = nullptr;
  esp_err_t err = esp_lcd_rgb_panel_get_frame_buffer(handle, 1, &fb);
  if (err != ESP_OK || !fb) {
    ESP_LOGE("screenshot", "get_frame_buffer: %s", esp_err_to_name(err));
    return nullptr;
  }
  return fb;
}

// ── Screenshot Capture ──────────────────────────────────────────────
static void capture_and_send(esphome::display::Display *display) {
  uint32_t t0 = millis();

  void *fb = get_framebuffer(display);
  if (!fb) return;

  int w = display->get_width();
  int h = display->get_height();
  auto *pixels = (const uint16_t *) fb;
  size_t total = (size_t) w * h;
  if (total == 0) return;

  // Pass 1: count RLE pairs + compute CRC
  size_t rle_pairs = 0;
  uint32_t crc = 0;
  {
    uint16_t prev = pixels[0];
    uint16_t run = 1;
    for (size_t i = 1; i < total; i++) {
      if (pixels[i] == prev && run < 65535) { run++; continue; }
      rle_pairs++;
      uint8_t p[4] = {(uint8_t)(run & 0xFF), (uint8_t)(run >> 8),
                       (uint8_t)(prev & 0xFF), (uint8_t)(prev >> 8)};
      crc = crc32_update(crc, p, 4);
      prev = pixels[i]; run = 1;
    }
    rle_pairs++;
    uint8_t p[4] = {(uint8_t)(run & 0xFF), (uint8_t)(run >> 8),
                     (uint8_t)(prev & 0xFF), (uint8_t)(prev >> 8)};
    crc = crc32_update(crc, p, 4);
  }

  uint32_t payload = (uint32_t)(rle_pairs * 4);
  uint32_t raw = total * 2;
  uint32_t t_enc = millis() - t0;

  ESP_LOGI("screenshot", "Capture %d×%d: %lu pairs, %lu→%lu B (%.1fx), encode %lu ms",
           w, h, (unsigned long) rle_pairs, (unsigned long) raw,
           (unsigned long) payload, (float) raw / payload, (unsigned long) t_enc);

  suppress_logging();

  // START marker (version 2 — with CRC)
  const uint8_t marker_s[] = {0x89, 'S', 'C', 'R', 0x02};
  uart_write_bytes(UART_PORT, marker_s, sizeof(marker_s));

  // Header: w(2) + h(2) + payload(4) + crc(4) = 12 bytes
  uint8_t hdr[12];
  uint16_t ww = w, hh = h;
  memcpy(&hdr[0], &ww, 2);  memcpy(&hdr[2], &hh, 2);
  memcpy(&hdr[4], &payload, 4); memcpy(&hdr[8], &crc, 4);
  uart_write_bytes(UART_PORT, hdr, sizeof(hdr));

  // Pass 2: stream RLE chunks
  uint8_t chunk[512];
  size_t cpos = 0;
  uint16_t prev = pixels[0];
  uint16_t run = 1;

  for (size_t i = 1; i <= total; i++) {
    bool fl = (i == total) || (pixels[i] != prev) || (run >= 65535);
    if (fl) {
      chunk[cpos++] = run & 0xFF;    chunk[cpos++] = run >> 8;
      chunk[cpos++] = prev & 0xFF;   chunk[cpos++] = prev >> 8;
      if (cpos >= sizeof(chunk) - 4 || i == total) {
        uart_write_bytes(UART_PORT, chunk, cpos);
        cpos = 0;
      }
      if (i < total) { prev = pixels[i]; run = 1; }
    } else { run++; }
  }

  const uint8_t marker_e[] = {0x89, 'E', 'N', 'D'};
  uart_write_bytes(UART_PORT, marker_e, sizeof(marker_e));
  uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(60000));

  restore_logging();

  uint32_t t_total = millis() - t0;
  ESP_LOGI("screenshot", "Done: encode=%lu ms, TX=%lu ms, total=%lu ms",
           (unsigned long) t_enc, (unsigned long)(t_total - t_enc),
           (unsigned long) t_total);
}

// ── Render Benchmark ────────────────────────────────────────────────
static void run_benchmark(esphome::display::Display *display) {
  ESP_LOGI("screenshot", "=== RENDER BENCHMARK ===");
  size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t heap_before = esp_get_free_heap_size();

  uint32_t t0 = millis();
  display->update();
  uint32_t dt = millis() - t0;

  size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t heap_after = esp_get_free_heap_size();

  char buf[512];
  snprintf(buf, sizeof(buf),
    "=== RENDER BENCHMARK ===\n"
    "Full redraw: %lu ms\n"
    "Heap: %+ld B (%lu→%lu)\n"
    "PSRAM: %+ld B (%lu→%lu)\n"
    "Free PSRAM: %lu B, largest block: %lu B\n",
    (unsigned long) dt,
    (long)(heap_after - heap_before),
    (unsigned long) heap_before, (unsigned long) heap_after,
    (long)(psram_after - psram_before),
    (unsigned long) psram_before, (unsigned long) psram_after,
    (unsigned long) psram_after,
    (unsigned long) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
  ESP_LOGI("screenshot", "%s", buf);
  send_text(buf);
}

// ── Main Dispatch ───────────────────────────────────────────────────
static void process_commands(esphome::display::Display *display) {
  char cmd = check_command();
  switch (cmd) {
    case 'S': capture_and_send(display); break;
    case 'I': send_system_info(); break;
    case 'B': run_benchmark(display); break;
    default: break;
  }
}

// ── Scene-Control Dispatch (with Dashboard access) ──────────────────
template<typename DashboardT>
static void process_commands(esphome::display::Display *display, DashboardT &db) {
  char cmd = check_command();
  switch (cmd) {
    case 'S': capture_and_send(display); break;
    case 'I': send_system_info(); break;
    case 'B': run_benchmark(display); break;
    case 'F': {
      if (db.is_fullscreen()) {
        db.exit_fullscreen();
        ESP_LOGI("screenshot", "Scene: exit fullscreen");
      } else {
        auto *t = db.first_tile_on_page();
        if (t) {
          db.enter_fullscreen(t);
          ESP_LOGI("screenshot", "Scene: enter fullscreen");
        }
      }
      display->update();
      break;
    }
    case 'P': {
      uint8_t old_page = db.active_page();
      db.next_page();
      ESP_LOGI("screenshot", "Scene: page %u→%u", old_page, db.active_page());
      display->update();
      break;
    }
    case 'R': {
      ESP_LOGI("screenshot", "Scene: automated run start");
      // 1) Grid view screenshot
      if (db.is_fullscreen()) { db.exit_fullscreen(); display->update(); }
      vTaskDelay(pdMS_TO_TICKS(100));
      capture_and_send(display);
      // 2) Fullscreen screenshot
      auto *t = db.first_tile_on_page();
      if (t) {
        db.enter_fullscreen(t);
        display->update();
        vTaskDelay(pdMS_TO_TICKS(100));
        capture_and_send(display);
        // 3) Back to grid
        db.exit_fullscreen();
        display->update();
        vTaskDelay(pdMS_TO_TICKS(100));
        capture_and_send(display);
      }
      ESP_LOGI("screenshot", "Scene: automated run done");
      break;
    }
    default: break;
  }
}

}  // namespace screenshot_helper
#endif
