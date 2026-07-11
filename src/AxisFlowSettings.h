#pragma once

#include <QtCore/QObject>

#include <Base/Parameter.h>

namespace AxisFlowGui {

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
    unsigned long liveLineColor()   const { return _liveLineColor; }   // 0xRRGGBBAA
    void setLiveLineEnabled(bool v);
    void setLiveLineStyle(int v);
    void setLiveLineColor(unsigned long rgba);

    // ---- hotkeys ----------------------------------------------------------
    /** Fill @p hk from the settings; actions with no stored key keep their
     *  built-in default, an explicitly stored 0 means "unbound". */
    void loadHotkeys(Hotkeys& hk);
    /** Persist every bound action of @p hk (0 for unbound ones). */
    void saveHotkeys(const Hotkeys& hk);

Q_SIGNALS:
    /** Any option changed; clients (compass, panel) should refresh. */
    void changed();

private:
    AxisFlowSettings();
    ~AxisFlowSettings() override = default;
    AxisFlowSettings(const AxisFlowSettings&) = delete;
    AxisFlowSettings& operator=(const AxisFlowSettings&) = delete;

    ParameterGrp::handle group() const;

    int           _compassSizePx     { 120 };
    int           _axisThicknessPx   { 2 };
    double        _axisStartPct      { 0.15 };
    double        _axisLengthPct     { 0.70 };
    unsigned long _axisColorX        { 0xFF0000FFul };   // red
    unsigned long _axisColorY        { 0x00FF00FFul };   // green
    unsigned long _activeFieldColor  { 0xDCDCDCFFul };   // slightly darker field
    double        _fillTransparency  { 0.65 };
    bool          _alwaysShowCompass { false };
    bool          _liveLineEnabled   { true };
    int           _liveLineStyle     { 1 };            // dashed
    unsigned long _liveLineColor     { 0xFFE633FFul };  // warm yellow
};

} // namespace AxisFlowGui
