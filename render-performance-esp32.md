# Render-Performance ESP32-S3 + ST7701S RGB Panel

## Hardware-Setup
- **MCU:** ESP32-S3 (dual-core, 240 MHz)
- **Display:** ST7701S RGB Panel, 480×480, DMA-basiert, 1 Framebuffer in PSRAM
- **RAM:** 8MB PSRAM (Framebuffer + PixelBuffer-Allokationen)
- **Panel-Typ:** RGB-Interface (nicht SPI) — der DMA-Controller liest kontinuierlich aus dem PSRAM-Framebuffer

## Kernproblem
Jeder `draw_pixel_at()`-Aufruf auf dem ST7701S ist ein separater Schreibzugriff in den PSRAM-Framebuffer. Komplexe Formen (Arcs, Kreise, Gauges) erzeugen tausende Einzel-Zugriffen. Das Display-Rendering blockiert den Main-Loop und führt zu sichtbarem Aufbau, Flickern und im schlimmsten Fall zu Watchdog-Timeouts oder `StoreProhibited`-Crashes.

## Bisherige Optimierungen

### 1. Globales `fill()` eliminiert
**Problem:** Bei jedem Seiten-/Modus-Wechsel wurde `it.fill(SCREEN_BACKGROUND)` aufgerufen — 230.400 Pixel (480×480) in den PSRAM geschrieben. Das erzeugte einen sichtbaren schwarzen Blitz.

**Lösung:** `fill()` komplett entfernt. Stattdessen füllt jedes Tile seinen eigenen Bereich in `render_full()` via `filled_rectangle(x0, y0, w, h, SCREEN_BACKGROUND)`. Tiles übermalen sich selbst vollständig.

**Ergebnis:** Schwarzer Blitz bei Transitions eliminiert.

**Status:** ✅ Implementiert, funktioniert

### 2. Sofort-Redraw nach Touch
**Problem:** Nach Touch/Release auf ein Tile wurde kein sofortiger Redraw getriggert. Das Display wartete bis zum nächsten `update_interval` (bis zu 1s).

**Lösung:** `display_->update()` direkt nach Touch-Modus-Wechseln (Enter/Exit Fullscreen, Page-Wechsel).

**Status:** ✅ Implementiert

### 3. PixelBuffer — Offscreen RGB565 Rendering
**Problem:** Climate-Arc (100× `filled_circle`) und Gauge-Arc (60× `filled_triangle` / Dreiecks-Fan) erzeugen jeweils tausende PSRAM-Zugriffe.

**Lösung:** `pixel_buffer.h` — Software-Renderbuffer in PSRAM:
- Allokiert `uint16_t[]` via `new (std::nothrow)`
- Rendert Kreise/Arcs in den Buffer (reine RAM-Operationen, kein DMA)
- `blit()` = ein einziger `draw_pixels_at()`-Aufruf (1 DMA-Transfer statt 1000+)

**Methoden:**
- `ensure(w, h)` — Allokation/Resize
- `clear(color565)` — Buffer füllen
- `filled_circle(cx, cy, r, color565)` — Kreis in Buffer
- `filled_arc(cx, cy, outer_r, inner_r, start, end, color565)` — Ring-Segment
- `blit(display, x, y)` — Auf Display transferieren

**Wichtig:** `big_endian = false` in `blit()` ist kritisch:
- SDL-Simulator hat einen Fast-Path (`SDL_UpdateTexture`) der nur bei `big_endian=false`, `COLOR_BITNESS_565`, rotation=0 aktiv wird
- `big_endian=true` verursacht (a) Farb-Korruption (Pink statt Grau) und (b) SDL fällt auf per-Pixel `draw_pixel_at` zurück → Performance-Gewinn komplett genullt

**Status:** ✅ Implementiert, funktioniert im Simulator

### 4. PixelBuffer Größen-Limit für ESP32
**Problem:** In Fullscreen (480×480) wird die Climate-Tile riesig → Arc-Buffer ~361×310 = 112.000 Pixel = 224KB. Das PSRAM-Allokation + DMA-Transfer für so einen großen Buffer kann zu Crashes/Langsamkeit führen.

**Lösung:** `MAX_BUF_PIXELS = 256 * 128` (ca. 64KB). Wenn der berechnete Buffer größer ist, fällt die Tile automatisch auf den Direkt-Rendering-Fallback zurück (ohne PixelBuffer).

**Status:** ✅ Implementiert. **Aber:** Der Fallback (direkte `filled_circle` × N) ist auf dem ESP32 immer noch langsam. Besonders in Fullscreen.

### 5. Fullscreen `filled_rectangle` Skip
**Problem:** `render_full()` schrieb zuerst `filled_rectangle(0, 0, 480, 480, SCREEN_BG)` = 230K Pixel, bevor der Content darüber gezeichnet wurde. In Fullscreen ist das komplett redundant.

**Lösung:** `render_full()` überspringt die `filled_rectangle(SCREEN_BACKGROUND)` wenn `is_fullscreen()` — `render_background()` und `draw_content()` decken den gesamten Bereich ab.

**Status:** ✅ Implementiert

### 6. Re-Entrancy Prevention
**Problem:** `request_redraw()` rief direkt `display_->update()` auf. Wenn ein Tile-Callback (z.B. Switch `on_state_callback`, Climate `payload_callback`) während eines Render-Cycles `request_redraw()` aufrief, führte das zu verschachtelten `render_()` → `draw()` → `render_full()` Aufrufen → `StoreProhibited` Crash.

**Crash-Stack:**
```
StoreProhibited at render_() → fill()
  ← SwitchTile::SwitchTile() (add_touch_handler)
  ← ClimateTile::toggle_mode()
  ← Dashboard::next_page()
  ← release()
```

**Lösung:** `request_redraw()` setzt jetzt nur ein `redraw_pending_` Flag + `invalidate_cache()`. Eine neue `loop()` Methode im Component konsumiert die Flags und ruft `display_->update()` einmalig pro Zyklus auf.

**Status:** ✅ Implementiert, Crash behoben

### 7. Segment-Reduktion (Fallback)
**Problem:** Im Direkt-Rendering-Fallback (wenn PixelBuffer zu groß) werden immer noch viele einzelne `filled_circle` DMA-Transfers ausgeführt.

**Lösung:** Segment-Anzahl dynamisch: `min(100, max(30, radius/3))`. In Fullscreen (Radius ~192) werden ~64 statt 100 Kreise gezeichnet.

**Status:** ✅ Implementiert, aber grundlegend immer noch langsam auf ESP32

## Bekannte offene Probleme

### A. Fullscreen Climate immer noch langsam
In Fullscreen fällt der PixelBuffer über das 64KB-Limit → Fallback auf 64 einzelne `filled_circle(radius=~8)`. Jeder ist ein separater DMA-Transfer. Plus `drawFullBg()` zeichnet 5× `filled_rectangle` + 4× `filled_circle` (Rounded Rect). In Summe ~70+ DMA-Transfers für einen Frame.

**Mögliche Lösungen:**
1. PixelBuffer-Limit erhöhen (z.B. 256KB) — PSRAM hat genug Platz, aber DMA-Transfer für 256KB Buffer ist auch nicht instant
2. Tiled Rendering: Buffer in Streifen aufteilen (z.B. 480×32 = 30KB pro Streifen), jeden Streifen einzeln rendern und blitten
3. Fullscreen-Arc mit weniger Segmenten zeichnen (z.B. 20 statt 64) — sieht gröber aus
4. `draw_pixels_at` Performance auf ESP32 prüfen — ist der DMA-Transfer selbst der Bottleneck oder der PSRAM-Zugriff?

### B. Fullscreen-Background redundant
`drawFullBg()` zeichnet 480×480 Rounded Rect mit Border — das sind 5× `filled_rectangle` + 4× `filled_circle` + nochmal für den inneren Bereich. In Fullscreen könnte man auf Rounded Corners und Border komplett verzichten (einfach `filled_rectangle` für den gesamten Bereich).

### C. Switch-Tile Grafikfehler
Beim Umschalten eines Switches tritt gelegentlich ein Grafikfehler auf, gefolgt von Display-Blackout und komplettem Neuaufbau. Die Re-Entrancy-Prevention (Punkt 6) sollte das eigentlich beheben — muss auf ESP32 verifiziert werden.

### D. Cache-Invalidierung bei Fullscreen-Ein/Austritt
Beim Eintritt/Austritt aus Fullscreen wird `invalidate_all_()` aufgerufen → alle Cache-Keys gelöscht → alle Tiles `is_initial=true` → `render_full()` für jedes Tile. Das bedeutet beim Zurückkehren zur Grid-Ansicht werden alle 4 Tiles komplett neu gezeichnet statt nur die, die sich geändert haben.

## Performance-Zahlen (geschätzt)

| Operation | Pixel-Schreibzugriffe | DMA-Transfers |
|---|---|---|
| Grid 2×2 initial (4 Tiles) | ~230K (4× Tile-Background) | ~40 (BG + Content pro Tile) |
| Grid 2×2 Update (1 Tile) | ~10K (Value-Clip Area) | ~5 |
| Fullscreen Enter (Climate) | ~460K (BG + Arc + Text) | ~75 |
| Fullscreen Update (Temp-Drag) | ~50K (Value-Clip + Arc) | ~70 |
| Page-Wechsel | ~230K (4× Tile-Background) | ~40 |

## Architektur-Übersicht

```
Touch Event
  → TileDashboardComponent::touch() / release()
    → Dashboard::dispatch_touch() / enter_fullscreen()
    → display_->update()  [sofort-Redraw]

Display update_interval (1s) oder display_->update()
  → render_(it)
    → configure_context_()  [Grid-Parameter prüfen]
    → draw_status_bar_(it)   [nur bei >1 Page]
    → dashboard_.draw(it)
      → Tile::draw(it)
        → make_cache_key() → Dirty-Check
        → render_full(it)   [initial: BG + Labels + Content]
        → render_update(it) [nur Content-Bereich]

Tile-Callbacks (Switch on_state, Climate payload)
  → request_redraw()  [setzt Flag, KEIN display_->update()]
  → loop()            [konsumiert Flag → display_->update()]
```

## Dev-Lead-Analyse (31.03.2026)

### Kernbefund
Der eigentliche Engpass ist nicht „ST7701S ist langsam", sondern **ESPHome-Native-Rendering erzeugt auf diesem Treiber extrem viel Write-Amplification auf einen RGB-Framebuffer in PSRAM**. Der ESPHome-ST7701S-Treiber nutzt 1 PSRAM-Framebuffer + 10-Zeilen-Bounce-Buffer (`bounce_buffer_size_px = width * 10`, `num_fbs = 1`). Trotzdem ist der Renderpfad teuer:

- `draw_pixel_at()` ist im Treiber als _„horribly slow"_ markiert
- Intern wird pro Pixel ein `1×1 draw_pixels_at()` ausgelöst + Watchdog-Feed
- Alle generischen ESPHome-Primitives (`filled_rectangle()`, `horizontal_line()`, `filled_circle()`) bauen auf diesen Pixelpfad auf

**Das Problem ist primär CPU-/Software-Rasterisierung + PSRAM-Zugriffsmuster, nicht der Panel-Scanout.**

### Einordnung des PixelBuffer-Ansatzes
Der PixelBuffer/Blit-Ansatz ist **architektonisch genau die richtige Richtung**. Im Treiber ist `draw_pixels_at()` dann schnell, wenn:
- Der Quellpuffer **zusammenhängend** ist
- `x_offset`, `y_offset` und `x_pad` alle **0** sind
- → dann 1× `esp_lcd_panel_draw_bitmap()` statt zeilenweiser Fallback

**Wichtig:** `big_endian=false` ist für den SDL-Simulator relevant (Fast-Path). Auf dem ESP32 setzt der Treiber passendes Datenlayout voraus — der Performancevorteil kommt dort vom contiguous Blit, nicht vom Endian-Flag.

### Panel-Timing (kein Hauptlimit)
- Default-Timings: `hsync 10/10/20`, `vsync 10/10/10`, PCLK 16 MHz → ~60 Hz Scanout
- ESPHome-Doku nennt 8 MHz (= ~30 Hz) — Board-Config prüfen
- **Sichtbare Aufbaueffekte kommen vom Rendern, nicht vom Refresh-Takt**

### Bewertung der bisherigen Maßnahmen

| Maßnahme | Bewertung | Kommentar |
|---|---|---|
| fill() entfernt | ✅ Sehr gut | `filled_rectangle()` ist intern Pixel-Schleife. **Prüfen: `auto_clear_enabled` muss aus sein!** |
| Sofort-Redraw | ✅ Gut für UX | Kein Durchsatzgewinn. Dazu passt `update_interval: never`. |
| PixelBuffer/Offscreen | ✅ **Größter Hebel** | Viele Primitive → 1 Blocktransfer |
| 64KB-Limit | ⚠️ Defensiv, aber... | Sobald Fallback greift, verliert man fast den ganzen Vorteil |
| Fullscreen-BG-Skip | ✅ Klarer Gewinn | Fullscreen-Clears besonders teuer |
| Re-Entrancy-Fix | ✅ Essenziell | Nested Renders brandgefährlich, Treiber-Kosten verschärfen Folgen |
| Segment-Reduktion | ⚠️ Symptom | Grundkrankheit bleibt die Primitive-Flut |

### Priorisierte Empfehlungen

**Prio 1: Strip-basierter PixelBuffer**
- Statt Fall-back auf direkte Primitives: Arc in horizontale Streifen (z.B. 480×32 = 30KB) aufteilen
- Jeder Streifen contiguous → schneller `draw_bitmap()`-Pfad
- Buffer bleibt unter 64KB, kein OOM-Risiko

**Prio 2: Fullscreen-Background radikal vereinfachen**
- In Fullscreen: KEINE Rounded Corners, KEIN Border → einfach `filled_rectangle(TILE_BG)`
- Rounded-Rects auf diesem Stack = viele Pixel-/Linienoperationen, reiner Overhead

**Prio 3: Statische/dynamische Teile trennen**
- Track, Rahmen, Labels, statische Hintergründe → nur einmal zeichnen
- Nur Füllstand/Marker/Wert bei Änderung neu rendern

**Prio 4: Flash/NVS-Writes aus Interaktionspfaden rausziehen**
- PSRAM und Flash teilen sich Ressourcen (Espressif-Doku)
- NVS-/FATFS-Schreibzugriffe → UI-Glitches + Blackout möglich
- Switch-Callbacks prüfen: lösen die mittelbar Flash-Last aus?

**Prio 5: Grid-/Fullscreen-Caches entkoppeln**
- Aktuell: `invalidate_all_()` bei Fullscreen-Ein/Austritt → teuerster Renderpfad genau im sichtbarsten Moment
- Besser: Getrennte Cache-Domänen oder nur betroffenen Tile invalidieren

**Prio 6: `auto_clear_enabled: false` + `update_interval: never`**
- Alle Redraws explizit, keine Framework-Automatismen

### Referenz-Benchmarks (aus Community/Espressif)
- ESP32-S3 + RGB 800×480 + LVGL + 2 PSRAM-FB: ~14 FPS (Music-Demo)
- Espressif-LVGL-Performance auf 800×480 RGB: 12-16 avg FPS nach Tuning
- 480×480 hat weniger Pixel, aber nativer ESPHome-Zeichenpfad > schlechter als LVGL
- **Flüssiges 30-60 FPS Fullscreen-Arc-Dragging ist in nativem ESPHome nicht realistisch**

### PSRAM-Bandbreite (aus Espressif-Doku)
- Octal PSRAM: ~22 MHz PCLK-Limit (80 MHz), ~30 MHz (120 MHz)
- Quad PSRAM: ~11 MHz PCLK-Limit — deutlich schlechter, Flackern möglich
- **Board auf Octal prüfen — für RGB-Panels klar vorzuziehen**

### Gesamturteil
> Mit ESPHome auf ESP32-S3 + ST7701S RGB ist eine gute, alltagstaugliche 480×480-Touch-UI absolut machbar, solange sie **widget-/kachelartig**, **dirty-rect-orientiert** und **block-transfer-freundlich** gebaut ist. Was nicht gut skaliert: Fullscreen-Redraws mit vielen nativen ESPHome-Primitives (Kreise, Arcs, Dreiecks-Fans, Rounded-Rects).

### Nächster Schritt: Benchmark-Matrix
| Test | Methode |
|---|---|
| `filled_rectangle(480×480)` | Baseline: wie teuer ist ein Full-Screen Clear? |
| `blit(480×32)` | Strip-Transfer, 30KB |
| `blit(480×64)` | Strip-Transfer, 60KB |
| Fullscreen-Arc Fallback | Aktueller Direkt-Primitive-Pfad |
| Fullscreen-Arc Strip-Renderer | Neuer Strip-basierter Pfad |

→ Danach ist klar, ob ESPHome-Stack mit Strip-Buffer reicht oder LVGL/IDF nötig wird.

---

## Hardware-Benchmark-Ergebnisse (01.04.2026)

### Teststrecke
ESP32-S3, 2×2 Grid (2× Climate, 1× Switch, 1× Gauge), 240×240 pro Tile.
Firmware: `esp32_benchmark.yaml`, Fake-Sensoren mit 1s Update-Intervall.

### Iteration 1: Baseline (vor Optimierung)

```
Climate[0] FULL 240x240: 837ms  (fill=277, bg=540, lbl=3, content=18)
Climate[1] FULL 240x240: 838ms  (fill=277, bg=540, lbl=3, content=17)
Switch[2]  FULL 240x240: 1062ms (fill=277, bg=540, lbl=3, content=242)
Gauge[3]   FULL 240x240: 1006ms (fill=277, bg=541, lbl=3, content=186) [OOM-Fallback]
render_() gesamt:         2693ms
```

**Kernbefund:** 97% der Renderzeit für Hintergrund (277ms fill + 540ms rounded rect), nur 2-3% für Content.
- `filled_rectangle(240×240)` = 57.600 per-pixel PSRAM-Writes @ 4.8μs/px
- `drawFullBg()` = 2× `draw_rounded_rect` = 10× `filled_rectangle` + 8× `filled_circle` ≈ 100K PSRAM-Writes
- `new (std::nothrow)` crasht auf ESP-IDF (kein Exception-Support) → `abort()`

### Iteration 2: PixelBuffer-Background + PSRAM-Allokation

**Änderungen:**
1. `render_full()` zeichnet Background via `blit_strips` (shared static PixelBuffer, 240×32 Strips)
2. `malloc` → `heap_caps_malloc(MALLOC_CAP_SPIRAM)` für große Buffer
3. `filled_arc()`: `atan2()` durch Cross-Product-Winkeltest ersetzt
4. Gauge: 3 separate Arc-Passes → kombinierter `draw_gauge_arc()` Single-Pass

```
Climate[0] FULL 240x240:  34ms  (bg_blit=12, lbl=3, content=19)
Climate[1] FULL 240x240:  35ms  (bg_blit=12, lbl=3, content=20)
Gauge[3]   FULL 240x240: 198ms  (bg_blit=12, lbl=3, content=183)
Gauge[3]   cached:         16ms  (bg_blit=12, lbl=3, content=0)
render_() nur Climate:     70ms
render_() mit Gauge:      198ms
render_() cached:          16ms
```

### Speedup-Zusammenfassung

| Metrik | Vorher | Nachher | Speedup |
|---|---|---|---|
| Climate FULL | 837ms | **34ms** | **25×** |
| Background (fill+bg) | 817ms | **12ms** (bg_blit) | **68×** |
| Gauge Arc (content) | 267ms (3-pass atan2) | **183ms** (single-pass cross-product) | **1.5×** |
| render_() 4 Tiles | 2693ms | **198ms** | **14×** |
| render_() Cache-Hit | 2693ms | **16ms** | **168×** |

### Verbleibende Engpässe
- **Gauge Arc-Rendering (183ms):** Float-Berechnungen (d², cross-products) für ~34K Pixel. ESP32-S3 hat FPU, aber die reine Pixel-Iteration ist der Limiter. Mögliche nächste Schritte: Integer-Arithmetik, Row-basierte Scanline statt Bounding-Box.
- **Labels (3ms):** Font-Rendering nutzt noch per-pixel Pfad. Könnte via PixelBuffer-Text-Blit beschleunigt werden.
- **Climate content blit (19ms):** Arc-Kreise sind effizient (blit_strips 6-10ms + Kreisberechnung).
