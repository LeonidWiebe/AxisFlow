#include "PreCompiled.h"

#include "AxisFlowSettings.h"
#include "AxisFlowHotkeys.h"

#include <App/Application.h>

#include <algorithm>
#include <string>
#include <vector>

namespace AxisFlowGui {

namespace {
const char* kPath = "User parameter:BaseApp/Preferences/Mod/AxisFlow";

// Defaults kept in one place so load() and resetDefaults() cannot drift apart.
constexpr int    kDefCompassSize   = 120;
constexpr int    kDefAxisThickness = 2;
constexpr double kDefAxisStartPct  = 0.15;
constexpr double kDefAxisLengthPct = 0.70;
constexpr unsigned long kDefColorX = 0xFF0000FFul;   // red
constexpr unsigned long kDefColorY = 0x00FF00FFul;   // green
constexpr double kDefFillTransp    = 0.65;
constexpr bool   kDefAlwaysShow    = false;

double clamp01(double v)
{
    return std::max(0.0, std::min(1.0, v));
}
} // namespace

// ---------------------------------------------------------------------------

AxisFlowSettings& AxisFlowSettings::instance()
{
    static AxisFlowSettings inst;
    return inst;
}

AxisFlowSettings::AxisFlowSettings()
{
    load();
}

ParameterGrp::handle AxisFlowSettings::group() const
{
    return App::GetApplication().GetParameterGroupByPath(kPath);
}

// ---------------------------------------------------------------------------

void AxisFlowSettings::load()
{
    ParameterGrp::handle g = group();

    _compassSizePx    = static_cast<int>(g->GetInt("CompassSize",   kDefCompassSize));
    _axisThicknessPx  = static_cast<int>(g->GetInt("AxisThickness", kDefAxisThickness));
    _axisStartPct     = clamp01(g->GetFloat("AxisStartPct",  kDefAxisStartPct));
    _axisLengthPct    = clamp01(g->GetFloat("AxisLengthPct", kDefAxisLengthPct));
    _axisColorX       = g->GetUnsigned("AxisColorX", kDefColorX);
    _axisColorY       = g->GetUnsigned("AxisColorY", kDefColorY);
    _fillTransparency = clamp01(g->GetFloat("FillTransparency", kDefFillTransp));
    _alwaysShowCompass = g->GetBool("AlwaysShowCompass", kDefAlwaysShow);

    // Guard against a nonsensical stored size; a zero-size compass is invisible
    // and looks like a bug rather than a setting.
    if (_compassSizePx   < 8) _compassSizePx   = kDefCompassSize;
    if (_axisThicknessPx < 1) _axisThicknessPx = kDefAxisThickness;

    Q_EMIT changed();
}

void AxisFlowSettings::save()
{
    ParameterGrp::handle g = group();

    g->SetInt("CompassSize",   _compassSizePx);
    g->SetInt("AxisThickness", _axisThicknessPx);
    g->SetFloat("AxisStartPct",  _axisStartPct);
    g->SetFloat("AxisLengthPct", _axisLengthPct);
    g->SetUnsigned("AxisColorX", _axisColorX);
    g->SetUnsigned("AxisColorY", _axisColorY);
    g->SetFloat("FillTransparency", _fillTransparency);
    g->SetBool("AlwaysShowCompass", _alwaysShowCompass);
}

void AxisFlowSettings::resetDefaults()
{
    _compassSizePx     = kDefCompassSize;
    _axisThicknessPx   = kDefAxisThickness;
    _axisStartPct      = kDefAxisStartPct;
    _axisLengthPct     = kDefAxisLengthPct;
    _axisColorX        = kDefColorX;
    _axisColorY        = kDefColorY;
    _fillTransparency  = kDefFillTransp;
    _alwaysShowCompass = kDefAlwaysShow;
    save();

    // Hotkeys: drop the stored overrides so the built-in layout applies again.
    ParameterGrp::handle g = group();
    for (AxisAction a : Hotkeys::allActions())
        g->RemoveASCII((std::string("Hotkey.") + Hotkeys::name(a)).c_str());

    Q_EMIT changed();
}

// ---------------------------------------------------------------------------
// Setters: persist immediately, then notify. Each is a no-op if unchanged, so
// wiring a slider straight to a setter cannot cause a redraw storm.
// ---------------------------------------------------------------------------

void AxisFlowSettings::setCompassSizePx(int v)
{
    v = std::max(8, v);
    if (v == _compassSizePx) return;
    _compassSizePx = v;
    group()->SetInt("CompassSize", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setAxisThicknessPx(int v)
{
    v = std::max(1, v);
    if (v == _axisThicknessPx) return;
    _axisThicknessPx = v;
    group()->SetInt("AxisThickness", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setAxisStartPct(double v)
{
    v = clamp01(v);
    if (v == _axisStartPct) return;
    _axisStartPct = v;
    group()->SetFloat("AxisStartPct", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setAxisLengthPct(double v)
{
    v = clamp01(v);
    if (v == _axisLengthPct) return;
    _axisLengthPct = v;
    group()->SetFloat("AxisLengthPct", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setAxisColorX(unsigned long rgba)
{
    if (rgba == _axisColorX) return;
    _axisColorX = rgba;
    group()->SetUnsigned("AxisColorX", rgba);
    Q_EMIT changed();
}

void AxisFlowSettings::setAxisColorY(unsigned long rgba)
{
    if (rgba == _axisColorY) return;
    _axisColorY = rgba;
    group()->SetUnsigned("AxisColorY", rgba);
    Q_EMIT changed();
}

void AxisFlowSettings::setFillTransparency(double v)
{
    v = clamp01(v);
    if (v == _fillTransparency) return;
    _fillTransparency = v;
    group()->SetFloat("FillTransparency", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setAlwaysShowCompass(bool v)
{
    if (v == _alwaysShowCompass) return;
    _alwaysShowCompass = v;
    group()->SetBool("AlwaysShowCompass", v);
    Q_EMIT changed();
}

void AxisFlowSettings::unpackColor(unsigned long rgba, float& r, float& g, float& b, float& a)
{
    r = static_cast<float>((rgba >> 24) & 0xFF) / 255.0f;
    g = static_cast<float>((rgba >> 16) & 0xFF) / 255.0f;
    b = static_cast<float>((rgba >>  8) & 0xFF) / 255.0f;
    a = static_cast<float>((rgba      ) & 0xFF) / 255.0f;
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void AxisFlowSettings::loadHotkeys(Hotkeys& hk)
{
    ParameterGrp::handle g = group();

    for (AxisAction a : Hotkeys::allActions()) {
        const std::string key = std::string("Hotkey.") + Hotkeys::name(a);

        // Sentinel "-" distinguishes "never stored" (keep the built-in default)
        // from a stored empty string, which explicitly means "unbound".
        std::string stored = g->GetASCII(key.c_str(), "-");
        if (stored == "-")
            continue;

        // "16777220,16777221" -> both Return and numpad Enter.
        std::vector<int> keys;
        std::size_t pos = 0;
        while (pos < stored.size()) {
            std::size_t comma = stored.find(',', pos);
            if (comma == std::string::npos) comma = stored.size();
            const std::string tok = stored.substr(pos, comma - pos);
            if (!tok.empty()) {
                try { keys.push_back(std::stoi(tok)); }
                catch (...) { /* skip a corrupt token rather than lose the rest */ }
            }
            pos = comma + 1;
        }
        hk.bind(a, keys);   // empty list = unbound
    }
}

void AxisFlowSettings::saveHotkeys(const Hotkeys& hk)
{
    ParameterGrp::handle g = group();

    for (AxisAction a : Hotkeys::allActions()) {
        const std::string key = std::string("Hotkey.") + Hotkeys::name(a);

        std::string val;
        for (int k : hk.keysFor(a)) {
            if (!val.empty()) val += ',';
            val += std::to_string(k);
        }
        g->SetASCII(key.c_str(), val.c_str());   // "" = unbound
    }
}

} // namespace AxisFlowGui
