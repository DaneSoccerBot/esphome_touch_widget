/********************************************************************************
 *  display_context.h  –  Zentrale Struktur für Grid-, Font- und Renderer-Infos
 *                       (wird von allen Tile- und Dashboard-Klassen benutzt)
 ********************************************************************************
 *  Einbinden:
 *     #include "display_context.h"
 *
 *  Singleton-Zugriff:
 *     auto &ctx = get_display_ctx();
 *
 *  Abhängigkeiten:
 *     • colors.h
 *     • gauge_tile_renderer.h
 *     • battery_icon_renderer.h
 *     • climate_tile_renderer.h
 *
 ********************************************************************************/
#ifndef DISPLAY_CONTEXT_H
#define DISPLAY_CONTEXT_H

#include <vector>
#include <string>
#include <cmath>

#include "esphome.h"
#include "esphome/components/font/font.h"

#include "colors.h"
#include "tile_bg_renderer.h"

// Kurzer Alias, damit der Code übersichtlicher bleibt
using esphome::font::Font;

/**
 *  DisplayContext
 *  --------------
 *  • Hält alle globalen Layout-Parameter (Display-Größe, Raster, Offsets)
 *  • Enthält Pointer auf die geladenen Roboto-Fonts
 *  • Stellt Renderer-Instanzen bereit (Background, Gauge, Battery, Climate)
 *  • Caching-Vector zum „Dirty-Checking“ pro Kachel
 */
struct DisplayContext
{
    // --- Raster / Geometrie --------------------------------------------------
    int scr_w{0}, scr_h{0}; ///< komplette Display-Auflösung in Pixeln
    int cols{1}, rows{1};   ///< Anzahl Spalten / Zeilen im Dashboard
    int x0{0}, y0{0};       ///< Pixel-Offset links oben (nützlich bei Rahmen)

    // --- Fonts ---------------------------------------------------------------
    Font *roboto_12{}, *roboto_14{}, *roboto_16{}, *roboto_18{},
        *roboto_20{}, *roboto_25{}, *roboto_30{}, *roboto_35{}, *roboto_40{},
        *roboto_45{}, *roboto_50{}, *roboto_60{}, *roboto_70{}, *roboto_80{},
        *roboto_90{};

    Font *font_value_compact{}; ///< automatisch aus Grid berechnet
    Font *font_label_compact{};
    Font *font_gauge_value{};

    // --- Renderer ------------------------------------------------------------
    TileBackgroundRenderer bg_renderer;

    // --- Cache („dirty checking“) --------------------------------------------
    std::vector<std::string> cache_value; ///< key je Tile-Index

    // ------------------------------------------------------------------------
    // Helper-Funktionen
    // ------------------------------------------------------------------------
    int tile_w() const { return scr_w / cols; }
    int tile_h() const { return scr_h / rows; }

    /**
     *  Liefert abhängig von der geforderten Punktgröße den passenden Roboto-Font
     *  (gleiche Heuristik wie im alten DisplayUtils-Code).
     */
    Font *get_font_for_size(float pt) const
    {
        if (pt < 13)
            return roboto_12;
        if (pt < 15)
            return roboto_14;
        if (pt < 17)
            return roboto_16;
        if (pt < 19)
            return roboto_18;
        if (pt < 21)
            return roboto_20;
        if (pt < 27.5f)
            return roboto_25;
        if (pt < 32.5f)
            return roboto_30;
        if (pt < 37.5f)
            return roboto_35;
        if (pt < 42.5f)
            return roboto_40;
        if (pt < 47.5f)
            return roboto_45;
        if (pt < 55)
            return roboto_50;
        if (pt < 65)
            return roboto_60;
        if (pt < 75)
            return roboto_70;
        if (pt < 85)
            return roboto_80;
        return roboto_90; // größter Font
    }

    void set_roboto_fonts(Font *f12, Font *f14, Font *f16, Font *f18,
                          Font *f20, Font *f25, Font *f30, Font *f35,
                          Font *f40, Font *f45, Font *f50, Font *f60,
                          Font *f70, Font *f80, Font *f90)
    {
        roboto_12 = f12;
        roboto_14 = f14;
        roboto_16 = f16;
        roboto_18 = f18;
        roboto_20 = f20;
        roboto_25 = f25;
        roboto_30 = f30;
        roboto_35 = f35;
        roboto_40 = f40;
        roboto_45 = f45;
        roboto_50 = f50;
        roboto_60 = f60;
        roboto_70 = f70;
        roboto_80 = f80;
        roboto_90 = f90;
    }

    /**
     *  Muss nach dem Setzen der Font-Pointer einmalig aufgerufen werden.
     *  Berechnet Grid-abhängige Schriftgrößen und gibt diese an Spezial-Renderer
     *  weiter (Climate- / Gauge-Tile).
     */
    void set_grid(int width, int height,
                  int grid_cols, int grid_rows,
                  int offset_x, int offset_y)
    {
        scr_w = width;
        scr_h = height;

        cols = grid_cols;
        rows = grid_rows == 0 ? 1 : grid_rows;

        x0 = offset_x;
        y0 = offset_y;

        const float size_value_compact = (scr_h / rows) * 0.30f;
        const float size_label_compact = (scr_h / rows) * 0.1f;
        const float size_gauge_value = (scr_h / rows) * 0.25f;

        font_value_compact = get_font_for_size(size_value_compact);
        font_label_compact = get_font_for_size(size_label_compact);
        font_gauge_value = get_font_for_size(size_gauge_value);

        // Cache für alle Tiles zurücksetzen
        cache_value.assign(cols * rows, "");
    }
};

// -----------------------------------------------------------------------------
// Singleton-Accessor  –  überall per get_display_ctx() erreichbar
// -----------------------------------------------------------------------------
inline DisplayContext &get_display_ctx()
{
    static DisplayContext ctx;
    return ctx;
}

#endif /* DISPLAY_CONTEXT_H */
