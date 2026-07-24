#pragma once

#include <QtCore/QObject>

#include <Base/Parameter.h>

#include <string>

namespace AxisFlowGui {

/**
 * Factory defaults, in one public place.
 *
 * They used to live as file-local constants in the .cpp, which meant the only
 * way to get at them was to actually reset the settings. The settings dialog
 * needs to READ them (its per-section "Restore defaults" button writes just
 * that section's options back), so they are exposed here and the .cpp
 * constants are now derived from them -- still a single source of truth.
 */
struct AxisFlowDefaults {
    static constexpr int    compassSize   = 100;
    static constexpr int    axisThickness = 4;
    static constexpr double axisStartPct  = 0.80;
    static constexpr double axisLengthPct = 0.35;
    static constexpr unsigned long axisColorX = 0xFF0000FFul;   // red
    static constexpr unsigned long axisColorY = 0x00FF00FFul;   // green
    static constexpr unsigned long activeFieldColor = 0xDCDCDCFFul;
    static constexpr double fillTransparency = 0.65;
    static constexpr bool   alwaysShowCompass = false;

    static constexpr bool   liveLineEnabled = false;
    static constexpr int    liveLineStyle   = 0;    // solid
    static constexpr unsigned long liveLineColor = 0x00AAFFFFul;
    static constexpr double liveLineTransparency = 0.50;

    // Compass path: the dashed poly-line breaking the prospective point down
    // into a leg in the compass plane plus a leg along the normal.
    static constexpr bool   compassPathEnabled = true;
    static constexpr int    compassPathStyle   = 1;    // dashed
    static constexpr unsigned long compassPathColor = 0xC8C8C8FFul;   // light grey

    static constexpr bool   useDistanceIndexing = true;
    static constexpr bool   useAxesIndexing     = true;
    static constexpr int    indexingTolerance   = 15;   // pixels
    static constexpr int    indexingAxisWeight  = 8;    // pixels
    static constexpr double indexingAxisTransparency = 0.50;

    static constexpr bool   useSnapping = true;
    static constexpr bool   smartNegativeDirection = true;
    static constexpr bool   focusAdvanceOnMove = true;
    static constexpr int    compassFollowDirection = 0;
    static constexpr double compassRotateAnimation = 0.5;
};


class Hotkeys;

/**
 * AxisFlowSettings
 *
 * All persistent options, stored in FreeCAD's own parameter tree under
 *   User parameter:BaseApp/Preferences/Mod/AxisFlow
 *
 * That tree IS an XML file (user.cfg) but lives in the user's config directory:
 * always writable (a system-wide Mod/ often is not), survives reinstalling the
 * module, is reachable identically from C++ and Python, and later gives us a
 * FreeCAD preferences page for free.
 *
 * Colours are packed 0xRRGGBBAA, matching FreeCAD's own convention.
 * Axis start/length are fractions (0..1) of the distance from the compass
 * centre to its contour, so they stay correct at any compass size.
 *
 * Emits changed() whenever anything is modified, so the compass and the panel
 * can redraw without polling.
 */
class AxisFlowSettings : public QObject
{
    Q_OBJECT

public:
    static AxisFlowSettings& instance();

    /** (Re)read every value from the parameter tree. */
    void load();
    /** Write every value back. Values are also written on each setter. */
    void save();
    /** Restore built-in defaults (compass options AND hotkeys) and persist. */
    void resetDefaults();

    // ---- compass geometry -------------------------------------------------
    int  compassSizePx()    const { return _compassSizePx; }
    int  axisThicknessPx()  const { return _axisThicknessPx; }
    /** Fraction (0..1) of centre->contour distance where an axis starts. */
    double axisStartPct()   const { return _axisStartPct; }
    /** Fraction (0..1) of centre->contour distance an axis spans. */
    double axisLengthPct()  const { return _axisLengthPct; }

    void setCompassSizePx(int v);
    void setAxisThicknessPx(int v);
    void setAxisStartPct(double v);
    void setAxisLengthPct(double v);

    // ---- colours / fill ---------------------------------------------------
    unsigned long axisColorX() const { return _axisColorX; }   // 0xRRGGBBAA
    unsigned long axisColorY() const { return _axisColorY; }
    /** Background of the coordinate field that is currently active. */
    unsigned long activeFieldColor() const { return _activeFieldColor; }
    /** 0 = opaque fill, 1 = fully transparent. */
    double fillTransparency()  const { return _fillTransparency; }

    void setAxisColorX(unsigned long rgba);
    void setAxisColorY(unsigned long rgba);
    void setActiveFieldColor(unsigned long rgba);
    void setFillTransparency(double v);

    /** Unpack a packed 0xRRGGBBAA colour into 0..1 floats. */
    static void unpackColor(unsigned long rgba, float& r, float& g, float& b, float& a);

    // ---- behaviour --------------------------------------------------------
    /** Show the compass all the time the module is loaded, not only while a
     *  command is running. */
    bool alwaysShowCompass() const { return _alwaysShowCompass; }
    void setAlwaysShowCompass(bool v);

    // ---- live guide line (compass centre -> cursor) -----------------------
    /** Draw the live guide line from the compass centre to the cursor. */
    bool          liveLineEnabled() const { return _liveLineEnabled; }
    /** 0 = solid, 1 = dashed, 2 = dotted, 3 = dash-dot. */
    int           liveLineStyle()   const { return _liveLineStyle; }
    /** Compass path: shown while an object snap is live or a coordinate field
     *  is frozen, as the visual breakdown of where the point actually sits. */
    bool          compassPathEnabled() const { return _compassPathEnabled; }
    int           compassPathStyle()   const { return _compassPathStyle; }
    unsigned long compassPathColor()   const { return _compassPathColor; }
    unsigned long liveLineColor()   const { return _liveLineColor; }   // 0xRRGGBBAA
    void setLiveLineEnabled(bool v);
    void setLiveLineStyle(int v);
    void setCompassPathEnabled(bool v);
    void setCompassPathStyle(int v);
    void setCompassPathColor(unsigned long rgba);
    void setLiveLineColor(unsigned long rgba);

    // ---- live-line indexing ----------------------------------------------
    /** Snap the live-line length to a remembered "indexed distance". */
    bool useDistanceIndexing() const { return _useDistanceIndexing; }
    /** Snap the live line onto the nearest in-plane axis (X or Y). */
    bool useAxesIndexing()     const { return _useAxesIndexing; }
    /** Snap tolerance in screen pixels (0..50). */
    int  indexingTolerance()   const { return _indexingTolerance; }
    /** Transparency of the ordinary live line, 0 opaque .. 1 invisible. */
    double liveLineTransparency() const { return _liveLineTransparency; }
    /** Line width of the candidate-axis line, in pixels. */
    int  indexingAxisWeight()  const { return _indexingAxisWeight; }
    /** Transparency of the candidate-axis line, 0 opaque .. 1 invisible. */
    double indexingAxisTransparency() const { return _indexingAxisTransparency; }

    void setUseDistanceIndexing(bool v);
    void setUseAxesIndexing(bool v);
    void setIndexingTolerance(int v);
    void setLiveLineTransparency(double v);
    void setIndexingAxisWeight(int v);
    void setIndexingAxisTransparency(double v);

    // ---- behaviour ---------------------------------------------------------
    /** Use FreeCAD's (Draft) object snapping inside AxisFlow tools. */
    bool useSnapping() const   { return _useSnapping; }
    void setUseSnapping(bool v);

    /** AccuDraw sign-from-direction: a bare magnitude typed into a local
     *  rectangular field takes the sign of the cursor's current direction
     *  along that axis (pointing down -X and typing "5" locks X = -5; the
     *  field shows the typed text until it loses focus, then reformats to the
     *  signed value). Off = the typed sign is used literally. Default on. */
    bool smartNegativeDirection() const { return _smartNegative; }
    void setSmartNegativeDirection(bool v);

    /** Focus advance on movement: after typing a value into an in-plane field,
     *  a deliberate cursor move finishes the entry and puts the caret into the
     *  adjacent field (X <-> Y; D <-> A in radial mode), so a pair of
     *  coordinates is typed in one flow. Default on. */
    bool focusAdvanceOnMove() const { return _focusAdvance; }
    void setFocusAdvanceOnMove(bool v);

    /** "Compass follows direction": after a point is confirmed, align the
     *  compass with the segment just drawn. 0 = off, 1 = on keeping the
     *  normal (in-plane rotation, Z stays), 2 = on fully (the segment
     *  direction becomes the compass +X). Default 0. */
    int  compassFollowDirection() const { return _compassFollow; }
    void setCompassFollowDirection(int v);

    /** Compass rotation animation, in seconds (0 = instant). Display only:
     *  the solver switches planes immediately, the compass eases after it. */
    double compassRotateAnimation() const { return _rotateAnimS; }
    void   setCompassRotateAnimation(double v);

    // ---- hotkeys ----------------------------------------------------------
    /** Fill @p hk from the settings; actions with no stored key keep their
     *  built-in default, an explicitly stored 0 means "unbound". */
    /** Apply the default key layout from an INI file on top of the built-in
     *  one. This is the DEFAULTS layer: user overrides (loadHotkeys) still win.
     *  Returns false if the file is missing or unreadable, in which case @p hk
     *  keeps the built-in layout. Problems are reported to the Report view. */
    bool loadHotkeyDefaultsFile(const std::string& path, Hotkeys& hk);

    void loadHotkeys(Hotkeys& hk);
    /** Persist every bound action of @p hk (0 for unbound ones). */
    void saveHotkeys(const Hotkeys& hk);

    /** Drop the user's stored key overrides so the defaults layer (built-in +
     *  hotkeys.ini) applies again. Other options are left untouched. */
    void clearStoredHotkeys();

Q_SIGNALS:
    /** Any option changed; clients (compass, panel) should refresh. */
    void changed();

private:
    AxisFlowSettings();
    ~AxisFlowSettings() override = default;
    AxisFlowSettings(const AxisFlowSettings&) = delete;
    AxisFlowSettings& operator=(const AxisFlowSettings&) = delete;

    ParameterGrp::handle group() const;

    int           _compassSizePx     { 100 };
    int           _axisThicknessPx   { 4 };
    double        _axisStartPct      { 0.80 };
    double        _axisLengthPct     { 0.35 };
    unsigned long _axisColorX        { 0xFF0000FFul };   // red
    unsigned long _axisColorY        { 0x00FF00FFul };   // green
    unsigned long _activeFieldColor  { 0xDCDCDCFFul };   // slightly darker field
    double        _fillTransparency  { 0.65 };
    bool          _alwaysShowCompass { false };
    bool          _liveLineEnabled   { false };
    int           _liveLineStyle     { 0 };            // solid
    bool          _compassPathEnabled { true };
    int           _compassPathStyle   { 1 };           // dashed
    unsigned long _compassPathColor   { 0xC8C8C8FFul };
    unsigned long _liveLineColor     { 0x00AAFFFFul };  // cyan-blue

    bool          _useDistanceIndexing { true };
    bool          _useAxesIndexing     { true };
    int           _indexingTolerance   { 15 };         // pixels, 0..50
    double        _liveLineTransparency { 0.50 };
    int           _indexingAxisWeight  { 8 };          // px
    double        _indexingAxisTransparency { 0.50 };

    bool          _useSnapping { true };
    bool          _smartNegative { true };
    bool          _focusAdvance { true };
    int           _compassFollow { 0 };
    double        _rotateAnimS { 0.5 };
};

} // namespace AxisFlowGui
