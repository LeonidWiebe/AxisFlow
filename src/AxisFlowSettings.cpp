#include "PreCompiled.h"

#include "AxisFlowSettings.h"
#include "AxisFlowHotkeys.h"

#include <App/Application.h>
#include <Base/Console.h>

#include <QtCore/QFile>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>
#include <QtGui/QKeySequence>

#include <algorithm>
#include <string>
#include <vector>

namespace AxisFlowGui {

namespace {
const char* kPath = "User parameter:BaseApp/Preferences/Mod/AxisFlow";

// Defaults kept in one place so load() and resetDefaults() cannot drift apart.
constexpr int    kDefCompassSize   = 100;
constexpr int    kDefAxisThickness = 4;
constexpr double kDefAxisStartPct  = 0.80;
constexpr double kDefAxisLengthPct = 0.35;
constexpr unsigned long kDefColorX = 0xFF0000FFul;   // red
constexpr unsigned long kDefColorY = 0x00FF00FFul;   // green
constexpr unsigned long kDefActiveField = 0xDCDCDCFFul;   // a touch darker than a plain field
constexpr double kDefFillTransp    = 0.65;
constexpr bool   kDefAlwaysShow    = false;
constexpr bool          kDefLiveLineEnabled = false;
constexpr int           kDefLiveLineStyle   = 0;   // solid
constexpr unsigned long kDefLiveLineColor   = 0x00AAFFFFul;   // cyan-blue

constexpr bool   kDefUseDistIdx     = true;
constexpr bool   kDefUseAxesIdx     = true;
constexpr int    kDefIndexTol       = 15;     // pixels
constexpr double kDefLiveLineTransp = 0.50;
constexpr int    kDefIdxAxisWeight  = 8;      // pixels
constexpr double kDefIdxAxisTransp  = 0.50;

constexpr bool   kDefUseSnapping = true;
constexpr bool   kDefSmartNegative = true;
constexpr bool   kDefFocusAdvance = true;
constexpr int    kDefCompassFollow = 0;

static int clampFollow(int v) { return v < 0 ? 0 : (v > 2 ? 2 : v); }
constexpr double kDefRotateAnim = 0.5;
static double clampAnim(double v) { return v < 0.0 ? 0.0 : (v > 2.0 ? 2.0 : v); }

int clampTol(int v)
{
    if (v < 0)  v = 0;
    if (v > 50) v = 50;
    return v;
}

/**
 * One key token from hotkeys.ini -> Qt key code, or 0 if not understood.
 * Accepts a Qt key name ("Insert", "Space", "F5"), a literal character ("X",
 * "`") or a raw code ("0x1000005", "16777221").
 *
 * A bare decimal is treated as a raw code only when it is clearly one (> 0xFF):
 * that keeps "1" meaning the digit-1 key rather than key code 1.
 */
int parseKeyToken(const QString& tokIn)
{
    const QString tok = tokIn.trimmed();
    if (tok.isEmpty())
        return 0;

    bool ok = false;
    if (tok.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        const int code = tok.mid(2).toInt(&ok, 16);
        if (ok && code != 0)
            return code;
    }
    else {
        const int code = tok.toInt(&ok, 10);
        if (ok && code > 0xFF)          // special keys only; digits fall through
            return code;
    }

    const QKeySequence seq = QKeySequence::fromString(tok, QKeySequence::PortableText);
    if (seq.count() == 1) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const int combined = seq[0].toCombined();
#else
        const int combined = static_cast<int>(seq[0]);
#endif
        // AxisFlow binds plain keys only -- drop any modifier bits. A plain int
        // mask on purpose: ~Qt::KeyboardModifierMask yields a QFlags in Qt6,
        // and "int & QFlags" does not compile.
        const int kModifierMask = 0xfe000000;
        const int key = combined & ~kModifierMask;
        // fromString() does NOT fail on nonsense: it yields Qt::Key_unknown.
        // Reject it, so a typo is reported instead of binding a dead key.
        if (key != static_cast<int>(Qt::Key_unknown))
            return key;
    }
    return 0;
}

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
    _activeFieldColor = g->GetUnsigned("ActiveFieldColor", kDefActiveField);
    _fillTransparency = clamp01(g->GetFloat("FillTransparency", kDefFillTransp));
    _alwaysShowCompass = g->GetBool("AlwaysShowCompass", kDefAlwaysShow);
    _liveLineEnabled   = g->GetBool("LiveLineEnabled", kDefLiveLineEnabled);
    _liveLineStyle     = static_cast<int>(g->GetInt("LiveLineStyle", kDefLiveLineStyle));
    _liveLineColor     = g->GetUnsigned("LiveLineColor", kDefLiveLineColor);
    if (_liveLineStyle < 0 || _liveLineStyle > 3) _liveLineStyle = kDefLiveLineStyle;

    _useDistanceIndexing = g->GetBool("UseDistanceIndexing", kDefUseDistIdx);
    _useAxesIndexing     = g->GetBool("UseAxesIndexing", kDefUseAxesIdx);
    _indexingTolerance   = clampTol(static_cast<int>(g->GetInt("IndexingTolerance", kDefIndexTol)));
    _liveLineTransparency = clamp01(g->GetFloat("LiveLineTransparency", kDefLiveLineTransp));
    _indexingAxisWeight  = static_cast<int>(g->GetInt("IndexingAxisWeight", kDefIdxAxisWeight));
    _indexingAxisTransparency = clamp01(g->GetFloat("IndexingAxisTransparency", kDefIdxAxisTransp));
    if (_indexingAxisWeight < 1) _indexingAxisWeight = kDefIdxAxisWeight;

    _useSnapping = g->GetBool("UseSnapping", kDefUseSnapping);
    _smartNegative = g->GetBool("SmartNegativeDirection", kDefSmartNegative);
    _focusAdvance = g->GetBool("FocusAdvanceOnMove", kDefFocusAdvance);
    _compassFollow = clampFollow(static_cast<int>(g->GetInt("CompassFollowDirection", kDefCompassFollow)));
    _rotateAnimS = clampAnim(g->GetFloat("CompassRotateAnimation", kDefRotateAnim));

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
    g->SetUnsigned("ActiveFieldColor", _activeFieldColor);
    g->SetFloat("FillTransparency", _fillTransparency);
    g->SetBool("AlwaysShowCompass", _alwaysShowCompass);
    g->SetBool("LiveLineEnabled", _liveLineEnabled);
    g->SetInt("LiveLineStyle", _liveLineStyle);
    g->SetUnsigned("LiveLineColor", _liveLineColor);

    g->SetBool("UseDistanceIndexing", _useDistanceIndexing);
    g->SetBool("UseAxesIndexing", _useAxesIndexing);
    g->SetInt("IndexingTolerance", _indexingTolerance);
    g->SetFloat("LiveLineTransparency", _liveLineTransparency);
    g->SetInt("IndexingAxisWeight", _indexingAxisWeight);
    g->SetFloat("IndexingAxisTransparency", _indexingAxisTransparency);
    g->SetBool("UseSnapping", _useSnapping);
    g->SetBool("SmartNegativeDirection", _smartNegative);
    g->SetBool("FocusAdvanceOnMove", _focusAdvance);
    g->SetInt("CompassFollowDirection", _compassFollow);
    g->SetFloat("CompassRotateAnimation", _rotateAnimS);
}

void AxisFlowSettings::resetDefaults()
{
    _compassSizePx     = kDefCompassSize;
    _axisThicknessPx   = kDefAxisThickness;
    _axisStartPct      = kDefAxisStartPct;
    _axisLengthPct     = kDefAxisLengthPct;
    _axisColorX        = kDefColorX;
    _axisColorY        = kDefColorY;
    _activeFieldColor  = kDefActiveField;
    _fillTransparency  = kDefFillTransp;
    _alwaysShowCompass = kDefAlwaysShow;
    _liveLineEnabled   = kDefLiveLineEnabled;
    _liveLineStyle     = kDefLiveLineStyle;
    _liveLineColor     = kDefLiveLineColor;
    _useDistanceIndexing = kDefUseDistIdx;
    _useAxesIndexing     = kDefUseAxesIdx;
    _indexingTolerance   = kDefIndexTol;
    _liveLineTransparency = kDefLiveLineTransp;
    _indexingAxisWeight  = kDefIdxAxisWeight;
    _indexingAxisTransparency = kDefIdxAxisTransp;
    _useSnapping = kDefUseSnapping;
    _smartNegative = kDefSmartNegative;
    _focusAdvance = kDefFocusAdvance;
    _compassFollow = kDefCompassFollow;
    _rotateAnimS = kDefRotateAnim;
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

void AxisFlowSettings::setActiveFieldColor(unsigned long rgba)
{
    if (rgba == _activeFieldColor) return;
    _activeFieldColor = rgba;
    group()->SetUnsigned("ActiveFieldColor", rgba);
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

void AxisFlowSettings::setLiveLineEnabled(bool v)
{
    if (v == _liveLineEnabled) return;
    _liveLineEnabled = v;
    group()->SetBool("LiveLineEnabled", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setLiveLineStyle(int v)
{
    if (v < 0 || v > 3) v = 1;
    if (v == _liveLineStyle) return;
    _liveLineStyle = v;
    group()->SetInt("LiveLineStyle", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setLiveLineColor(unsigned long rgba)
{
    if (rgba == _liveLineColor) return;
    _liveLineColor = rgba;
    group()->SetUnsigned("LiveLineColor", rgba);
    Q_EMIT changed();
}

void AxisFlowSettings::setUseDistanceIndexing(bool v)
{
    if (v == _useDistanceIndexing) return;
    _useDistanceIndexing = v;
    group()->SetBool("UseDistanceIndexing", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setUseAxesIndexing(bool v)
{
    if (v == _useAxesIndexing) return;
    _useAxesIndexing = v;
    group()->SetBool("UseAxesIndexing", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setIndexingTolerance(int v)
{
    v = clampTol(v);
    if (v == _indexingTolerance) return;
    _indexingTolerance = v;
    group()->SetInt("IndexingTolerance", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setLiveLineTransparency(double v)
{
    v = clamp01(v);
    if (v == _liveLineTransparency) return;
    _liveLineTransparency = v;
    group()->SetFloat("LiveLineTransparency", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setIndexingAxisWeight(int v)
{
    v = std::max(1, v);
    if (v == _indexingAxisWeight) return;
    _indexingAxisWeight = v;
    group()->SetInt("IndexingAxisWeight", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setIndexingAxisTransparency(double v)
{
    v = clamp01(v);
    if (v == _indexingAxisTransparency) return;
    _indexingAxisTransparency = v;
    group()->SetFloat("IndexingAxisTransparency", v);
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

bool AxisFlowSettings::loadHotkeyDefaultsFile(const std::string& path, Hotkeys& hk)
{
    QFile f(QString::fromUtf8(path.c_str()));
    if (!f.exists())
        return false;                     // no file: the built-in layout stands
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Base::Console().warning("AxisFlow: cannot read %s, using built-in hotkeys.\n",
                                path.c_str());
        return false;
    }

    QTextStream in(&f);
    int lineNo = 0;
    while (!in.atEnd()) {
        const QString raw = in.readLine();
        ++lineNo;

        const QString t = raw.trimmed();
        if (t.isEmpty()
            || t.startsWith(QLatin1Char(';'))
            || t.startsWith(QLatin1Char('#'))
            || t.startsWith(QLatin1Char('[')))     // comment, blank or [section]
            continue;

        const int eq = static_cast<int>(t.indexOf(QLatin1Char('=')));   // qsizetype -> int: an ini line never exceeds INT_MAX
        if (eq < 0) {
            Base::Console().warning("AxisFlow hotkeys.ini line %d: expected 'Action = Key'.\n",
                                    lineNo);
            continue;
        }

        const QString actName = t.left(eq).trimmed();
        QString value         = t.mid(eq + 1).trimmed();

        const AxisAction act = Hotkeys::fromName(actName.toStdString());
        if (act == AxisAction::None) {
            Base::Console().warning("AxisFlow hotkeys.ini line %d: unknown action '%s'.\n",
                                    lineNo, actName.toUtf8().constData());
            continue;
        }

        // A value of exactly one character IS that key, so ';' '#' ',' and '`'
        // can be bound without being eaten as a comment marker or separator.
        QStringList tokens;
        if (value.size() == 1) {
            tokens << value;
        }
        else {
            for (int i = 0; i < value.size(); ++i) {
                const QChar c = value.at(i);
                if (c == QLatin1Char(';') || c == QLatin1Char('#')) {
                    value = value.left(i).trimmed();   // strip inline comment
                    break;
                }
            }
            tokens = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        }

        std::vector<int> keys;
        for (const QString& tok : tokens) {
            const int code = parseKeyToken(tok);
            if (code != 0)
                keys.push_back(code);
            else
                Base::Console().warning("AxisFlow hotkeys.ini line %d: unknown key '%s'.\n",
                                        lineNo, tok.trimmed().toUtf8().constData());
        }

        hk.bind(act, keys);   // empty list = explicitly unbound
    }

    return true;
}

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

void AxisFlowSettings::setUseSnapping(bool v)
{
    if (v == _useSnapping) return;
    _useSnapping = v;
    group()->SetBool("UseSnapping", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setSmartNegativeDirection(bool v)
{
    if (v == _smartNegative) return;
    _smartNegative = v;
    group()->SetBool("SmartNegativeDirection", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setFocusAdvanceOnMove(bool v)
{
    if (v == _focusAdvance) return;
    _focusAdvance = v;
    group()->SetBool("FocusAdvanceOnMove", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setCompassFollowDirection(int v)
{
    v = clampFollow(v);
    if (v == _compassFollow) return;
    _compassFollow = v;
    group()->SetInt("CompassFollowDirection", v);
    Q_EMIT changed();
}

void AxisFlowSettings::setCompassRotateAnimation(double v)
{
    v = clampAnim(v);
    if (v == _rotateAnimS) return;
    _rotateAnimS = v;
    group()->SetFloat("CompassRotateAnimation", v);
    Q_EMIT changed();
}


void AxisFlowSettings::clearStoredHotkeys()
{
    ParameterGrp::handle g = group();
    for (AxisAction a : Hotkeys::allActions())
        g->RemoveASCII((std::string("Hotkey.") + Hotkeys::name(a)).c_str());
    Q_EMIT changed();
}

} // namespace AxisFlowGui
