#include "PreCompiled.h"

#include "AxisFlowCompass.h"
#include "AxisFlowSettings.h"
#include "AxisFlowPath.h"
#include "AxisFlowSolver.h"   // CompassMode

#include <Inventor/nodes/SoAnnotation.h>
#include <Inventor/nodes/SoLightModel.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoFaceSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoMaterial.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/nodes/SoShapeHints.h>
#include <Inventor/nodes/SoCallback.h>
#include <Inventor/actions/SoGLRenderAction.h>
#include <Inventor/system/gl.h>
#include <Inventor/SbMatrix.h>
#include <Inventor/SbRotation.h>
#include <Inventor/sensors/SoTimerSensor.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace AxisFlowGui {

namespace {

// Circle tessellation. 64 segments reads as smooth at any sane compass size.
constexpr int   kCircleSegs = 64;
constexpr float kPi = 3.14159265358979323846f;

// The fill colour is deliberately NOT a setting: only its transparency is.
// A pale blue keeps the compass readable over both light and dark models.
const SbColor kFillColor   (0.62f, 0.62f, 0.86f);
const SbColor kOutlineColor(0.25f, 0.25f, 0.30f);
const SbColor kDotColor    (0.10f, 0.10f, 0.10f);
const SbColor kGuideColor  (1.00f, 0.90f, 0.20f);

SbColor rgbOf(unsigned long rgba)
{
    float r, g, b, a;
    AxisFlowSettings::unpackColor(rgba, r, g, b, a);
    return SbColor(r, g, b);
}

// GL_POINTS are drawn as squares. These callbacks turn on point antialiasing
// (with blending) so a SoPointSet renders as a filled disc, then restore the
// exact prior GL state via push/pop so nothing leaks into the rest of the
// scene. Bracket a point set with beginRoundPoints .. endRoundPoints.
void beginRoundPoints(void*, SoAction* action)
{
    if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_ENABLE_BIT | GL_POINT_BIT);
    glEnable(GL_POINT_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void endRoundPoints(void*, SoAction* action)
{
    if (!action->isOfType(SoGLRenderAction::getClassTypeId())) return;
    glPopAttrib();
}

} // namespace

// ---------------------------------------------------------------------------

AxisFlowCompass::AxisFlowCompass()
{
    build();
    applySettings();
    rebuild();
}

void AxisFlowCompass::build()
{
    // SoAnnotation renders after the rest of the scene and ignores the depth
    // buffer, so the compass never z-fights with a face lying in the working
    // plane (an Arch wall at z=0 would otherwise swallow it).
    _root = new SoAnnotation;
    _root->ref();

    // One switch above everything: hiding the compass is a single field write,
    // and the subgraph keeps its state while hidden.
    _visSwitch = new SoSwitch;
    _visSwitch->whichChild.setValue(0);
    _root->addChild(_visSwitch);

    auto* content = new SoSeparator;
    _visSwitch->addChild(content);

    _transform = new SoTransform;
    content->addChild(_transform);

    // Overlay graphics must not be shaded: BASE_COLOR makes the rendered colour
    // exactly the colour we set, on any background and from any view angle.
    auto* lm = new SoLightModel;
    lm->model.setValue(SoLightModel::BASE_COLOR);
    content->addChild(lm);

    // The fill is a flat polygon seen from either side; without this it vanishes
    // when the working plane is viewed from behind.
    auto* hints = new SoShapeHints;
    hints->vertexOrdering.setValue(SoShapeHints::COUNTERCLOCKWISE);
    hints->shapeType.setValue(SoShapeHints::UNKNOWN_SHAPE_TYPE);
    content->addChild(hints);

    // ---- contour: square (child 0) / circle (child 1) ----
    _contour = new SoSwitch;
    _contour->whichChild.setValue(0);

    auto buildContour = [](SoMaterial*& fill, SoCoordinate3*& coordFill, SoFaceSet*& face,
                           SoCoordinate3*& coordLine, SoLineSet*& line, SoDrawStyle*& style)
    {
        auto* sep = new SoSeparator;

        fill = new SoMaterial;
        fill->diffuseColor.setValue(kFillColor);
        coordFill = new SoCoordinate3;
        face = new SoFaceSet;
        sep->addChild(fill);
        sep->addChild(coordFill);
        sep->addChild(face);

        auto* outColor = new SoBaseColor;
        outColor->rgb.setValue(kOutlineColor);
        style = new SoDrawStyle;
        style->lineWidth.setValue(1.5f);
        coordLine = new SoCoordinate3;
        line = new SoLineSet;
        sep->addChild(outColor);
        sep->addChild(style);
        sep->addChild(coordLine);
        sep->addChild(line);

        return sep;
    };

    _contour->addChild(buildContour(_fillSq, _coordSq, _faceSq, _outlineSq, _lineSq, _styleSq));
    _contour->addChild(buildContour(_fillCi, _coordCi, _faceCi, _outlineCi, _lineCi, _styleCi));
    content->addChild(_contour);

    // ---- centre point ----
    auto* dotSep = new SoSeparator;
    _dotColor = new SoBaseColor;
    _dotColor->rgb.setValue(kDotColor);
    _dotStyle = new SoDrawStyle;
    _dotStyle->pointSize.setValue(6.0f);
    _coordDot = new SoCoordinate3;
    _coordDot->point.set1Value(0, SbVec3f(0, 0, 0));
    _dot = new SoPointSet;
    _dot->numPoints.setValue(1);
    dotSep->addChild(_dotColor);
    dotSep->addChild(_dotStyle);
    dotSep->addChild(_coordDot);
    { auto* cb = new SoCallback; cb->setCallback(&beginRoundPoints); dotSep->addChild(cb); }
    dotSep->addChild(_dot);
    { auto* cb = new SoCallback; cb->setCallback(&endRoundPoints);   dotSep->addChild(cb); }
    content->addChild(dotSep);

    // ---- axes: X and Y only (no Z) ----
    auto buildAxis = [](SoBaseColor*& color, SoDrawStyle*& style,
                        SoCoordinate3*& coord, SoLineSet*& line)
    {
        auto* sep = new SoSeparator;
        color = new SoBaseColor;
        style = new SoDrawStyle;
        coord = new SoCoordinate3;
        line  = new SoLineSet;
        line->numVertices.setValue(2);
        sep->addChild(color);
        sep->addChild(style);
        sep->addChild(coord);
        sep->addChild(line);
        return sep;
    };

    content->addChild(buildAxis(_colorX, _styleX, _coordX, _lineX));
    content->addChild(buildAxis(_colorY, _styleY, _coordY, _lineY));

    // ---- guide line (live line) ----
    _guideSep = new SoSeparator;
    _matGuide = new SoMaterial;
    _matGuide->diffuseColor.setValue(kGuideColor);
    _matGuide->transparency.setValue(0.0f);
    _styleGuide = new SoDrawStyle;
    _styleGuide->lineWidth.setValue(1.0f);
    _styleGuide->linePattern.setValue(0xF0F0);   // dashed (overridden by settings)
    _coordGuide = new SoCoordinate3;
    _lineGuide = new SoLineSet;
    _lineGuide->numVertices.setValue(2);
    _guideSep->addChild(_matGuide);
    _guideSep->addChild(_styleGuide);
    _guideSep->addChild(_coordGuide);
    _guideSep->addChild(_lineGuide);
    content->addChild(_guideSep);

    // ---- candidate axis (replaces the live line when axis indexing snaps) ----
    _axisCandSwitch = new SoSwitch;
    _axisCandSwitch->whichChild.setValue(SO_SWITCH_NONE);
    {
        auto* sep = new SoSeparator;
        _matAxisCand = new SoMaterial;
        _matAxisCand->diffuseColor.setValue(kGuideColor);
        _matAxisCand->transparency.setValue(0.35f);
        _styleAxisCand = new SoDrawStyle;
        _styleAxisCand->lineWidth.setValue(2.0f);
        _styleAxisCand->linePattern.setValue(0xFFFF);
        _coordAxisCand = new SoCoordinate3;
        _lineAxisCand = new SoLineSet;
        _lineAxisCand->numVertices.setValue(0);
        sep->addChild(_matAxisCand);
        sep->addChild(_styleAxisCand);
        sep->addChild(_coordAxisCand);
        sep->addChild(_lineAxisCand);
        _axisCandSwitch->addChild(sep);
    }
    content->addChild(_axisCandSwitch);

    // ---- compass path (dashed breakdown of where the point really is) ------
    _pathSwitch = new SoSwitch;
    _pathSwitch->whichChild.setValue(SO_SWITCH_NONE);
    {
        auto* sep = new SoSeparator;
        _matPath = new SoMaterial;
        _matPath->diffuseColor.setValue(SbColor(0.78f, 0.78f, 0.78f));
        _stylePath = new SoDrawStyle;
        _stylePath->lineWidth.setValue(1.0f);      // thin on purpose: a read-out
        _stylePath->linePattern.setValue(0xF0F0);  // dashed
        _coordPath = new SoCoordinate3;
        _linePath = new SoLineSet;
        _linePath->numVertices.setValue(0);
        sep->addChild(_matPath);
        sep->addChild(_stylePath);
        sep->addChild(_coordPath);
        sep->addChild(_linePath);
        _pathSwitch->addChild(sep);
    }
    content->addChild(_pathSwitch);

    // ---- candidate point (distance-indexing snap marker at the line end) ----
    _candDotSwitch = new SoSwitch;
    _candDotSwitch->whichChild.setValue(SO_SWITCH_NONE);
    {
        auto* sep = new SoSeparator;
        _candDotColor = new SoBaseColor;
        _candDotColor->rgb.setValue(kDotColor);
        _candDotStyle = new SoDrawStyle;
        _candDotStyle->pointSize.setValue(6.0f);
        _candDotCoord = new SoCoordinate3;
        _candDotCoord->point.set1Value(0, SbVec3f(0, 0, 0));
        _candDot = new SoPointSet;
        _candDot->numPoints.setValue(1);
        sep->addChild(_candDotColor);
        sep->addChild(_candDotStyle);
        sep->addChild(_candDotCoord);
        { auto* cb = new SoCallback; cb->setCallback(&beginRoundPoints); sep->addChild(cb); }
        sep->addChild(_candDot);
        { auto* cb = new SoCallback; cb->setCallback(&endRoundPoints);   sep->addChild(cb); }
        _candDotSwitch->addChild(sep);
    }
    content->addChild(_candDotSwitch);
}

// ---------------------------------------------------------------------------

void AxisFlowCompass::applySettings()
{
    const AxisFlowSettings& s = AxisFlowSettings::instance();

    _animDurationS = static_cast<float>(s.compassRotateAnimation());

    const float t = static_cast<float>(s.axisThicknessPx());
    _styleX->lineWidth.setValue(t);
    _styleY->lineWidth.setValue(t);
    _colorX->rgb.setValue(rgbOf(s.axisColorX()));
    _colorY->rgb.setValue(rgbOf(s.axisColorY()));

    // A centre point thinner than the axes would look like a rendering glitch.
    _dotStyle->pointSize.setValue(std::max(4.0f, t * 2.5f));

    const float transp = static_cast<float>(s.fillTransparency());
    _fillSq->transparency.setValue(transp);
    _fillCi->transparency.setValue(transp);

    // Live guide line: on/off, dash style, colour, transparency.
    applyGuideStyle(s.liveLineEnabled(), s.liveLineStyle(), s.liveLineColor(),
                    s.liveLineTransparency());

    // Candidate axis inherits the live line's colour + pattern; its own weight
    // and transparency come from the indexing settings.
    if (_matAxisCand)
        _matAxisCand->diffuseColor.setValue(rgbOf(s.liveLineColor()));
    if (_styleAxisCand) {
        static const unsigned short kPat[4] = { 0xFFFF, 0xF0F0, 0xAAAA, 0xFF18 };
        const int idx = (s.liveLineStyle() < 0 || s.liveLineStyle() > 3) ? 1 : s.liveLineStyle();
        _styleAxisCand->linePattern.setValue(kPat[idx]);
    }
    applyCandidateAxisStyle(s.indexingAxisWeight(), s.indexingAxisTransparency());

    applyPathStyle(s.compassPathEnabled(), s.compassPathStyle(), s.compassPathColor());

    // Candidate point matches the centre dot's size.
    if (_candDotStyle)
        _candDotStyle->pointSize.setValue(std::max(4.0f, t * 2.5f));

    rebuild();   // axis start/length fractions may have changed too
}

void AxisFlowCompass::rebuild()
{
    const AxisFlowSettings& s = AxisFlowSettings::instance();
    const float R = _radius;

    // ---- square contour: corners at +/-R in the plane ----
    const SbVec3f sq[4] = {
        SbVec3f(-R, -R, 0), SbVec3f( R, -R, 0),
        SbVec3f( R,  R, 0), SbVec3f(-R,  R, 0)
    };
    _coordSq->point.setValues(0, 4, sq);
    _coordSq->point.setNum(4);
    _faceSq->numVertices.setValue(4);

    SbVec3f sqLoop[5] = { sq[0], sq[1], sq[2], sq[3], sq[0] };
    _outlineSq->point.setValues(0, 5, sqLoop);
    _outlineSq->point.setNum(5);
    _lineSq->numVertices.setValue(5);

    // ---- circle contour ----
    std::vector<SbVec3f> ci(kCircleSegs);
    for (int i = 0; i < kCircleSegs; ++i) {
        const float a = 2.0f * kPi * float(i) / float(kCircleSegs);
        ci[i] = SbVec3f(R * std::cos(a), R * std::sin(a), 0);
    }
    _coordCi->point.setValues(0, kCircleSegs, ci.data());
    _coordCi->point.setNum(kCircleSegs);
    _faceCi->numVertices.setValue(kCircleSegs);

    std::vector<SbVec3f> ciLoop(ci);
    ciLoop.push_back(ci[0]);
    _outlineCi->point.setValues(0, int(ciLoop.size()), ciLoop.data());
    _outlineCi->point.setNum(int(ciLoop.size()));
    _lineCi->numVertices.setValue(int(ciLoop.size()));

    // ---- axes: fractions of the centre-to-contour distance ----
    const float start = R * static_cast<float>(s.axisStartPct());
    const float end   = start + R * static_cast<float>(s.axisLengthPct());

    const SbVec3f xs[2] = { SbVec3f(start, 0, 0), SbVec3f(end, 0, 0) };
    _coordX->point.setValues(0, 2, xs);
    _coordX->point.setNum(2);

    const SbVec3f ys[2] = { SbVec3f(0, start, 0), SbVec3f(0, end, 0) };
    _coordY->point.setValues(0, 2, ys);
    _coordY->point.setNum(2);
}

void AxisFlowCompass::rebuildGuide()
{
    // The subgraph is positioned AND rotated by _transform, so everything here
    // is in compass-local coordinates: project the world delta onto the plane
    // basis (orthonormal, so a dot product is the inverse transform).
    const SbVec3f d = _cursor - _origin;
    _localEnd = SbVec3f(d.dot(_u), d.dot(_v), d.dot(_n));

    // The live line hides while the candidate axis stands in for it.
    const bool guideVisible = _guideEnabled && !_axisCandActive;
    if (guideVisible) {
        const SbVec3f pts[2] = { SbVec3f(0, 0, 0), _localEnd };
        _coordGuide->point.setValues(0, 2, pts);
        _coordGuide->point.setNum(2);
        _lineGuide->numVertices.setValue(2);
    } else {
        _lineGuide->numVertices.setValue(0);   // hidden
    }

    // Candidate axis: centre -> end. The end is already ON the axis, because
    // getSolvedPoint() perpendicular-projected it there before we got here.
    // It is the feedback for axis indexing and is ALWAYS shown when indexing
    // fires -- the compass path does not replace it (they answer different
    // questions: "you are riding this axis" vs "here is where the point sits").
    if (_axisCandSwitch) {
        if (_axisCandActive) {
            const SbVec3f pts[2] = { SbVec3f(0, 0, 0), _localEnd };
            _coordAxisCand->point.setValues(0, 2, pts);
            _coordAxisCand->point.setNum(2);
            _lineAxisCand->numVertices.setValue(2);
            _axisCandSwitch->whichChild.setValue(0);
        } else {
            _lineAxisCand->numVertices.setValue(0);
            _axisCandSwitch->whichChild.setValue(SO_SWITCH_NONE);
        }
    }

    // The path ends at the same point, so it moves with the read-out.
    rebuildPath();

    // Candidate point at the (snapped) live-line end.
    if (_candDotSwitch) {
        if (_distCandActive) {
            _candDotCoord->point.set1Value(0, _localEnd);
            _candDotSwitch->whichChild.setValue(0);
        } else {
            _candDotSwitch->whichChild.setValue(SO_SWITCH_NONE);
        }
    }
}

void AxisFlowCompass::applyGuideStyle(bool enabled, int style, unsigned long rgba, double transparency)
{
    _guideEnabled = enabled;

    if (_matGuide) {
        float r, g, b, a;
        AxisFlowSettings::unpackColor(rgba, r, g, b, a);
        _matGuide->diffuseColor.setValue(r, g, b);
        const float tr = static_cast<float>(transparency < 0.0 ? 0.0 : (transparency > 1.0 ? 1.0 : transparency));
        _matGuide->transparency.setValue(tr);
    }
    if (_styleGuide) {
        // solid, dashed, dotted, dash-dot
        static const unsigned short kPat[4] = { 0xFFFF, 0xF0F0, 0xAAAA, 0xFF18 };
        const int idx = (style < 0 || style > 3) ? 1 : style;
        _styleGuide->linePattern.setValue(kPat[idx]);
    }
    rebuildGuide();
}

void AxisFlowCompass::applyCandidateAxisStyle(int weightPx, double transparency)
{
    if (_styleAxisCand)
        _styleAxisCand->lineWidth.setValue(static_cast<float>(std::max(1, weightPx)));
    if (_matAxisCand) {
        const float tr = static_cast<float>(transparency < 0.0 ? 0.0 : (transparency > 1.0 ? 1.0 : transparency));
        _matAxisCand->transparency.setValue(tr);
    }
}

void AxisFlowCompass::setPathState(bool active, bool lockU, bool lockV,
                                   bool hasSnap, const SbVec3f& snapWorld)
{
    const bool same = (active == _pathActive)
                   && (lockU == _pathLockU)
                   && (lockV == _pathLockV)
                   && (hasSnap == _pathHasSnap)
                   && (snapWorld == _pathSnapWorld);
    if (same)
        return;

    _pathActive    = active;
    _pathLockU     = lockU;
    _pathLockV     = lockV;
    _pathHasSnap   = hasSnap;
    _pathSnapWorld = snapWorld;
    rebuildPath();
}

void AxisFlowCompass::applyPathStyle(bool enabled, int style, unsigned long rgba)
{
    _pathEnabled = enabled;
    if (_matPath) {
        float r, g, b, a;
        AxisFlowSettings::unpackColor(rgba, r, g, b, a);
        _matPath->diffuseColor.setValue(r, g, b);
    }
    if (_stylePath) {
        static const unsigned short kPat[4] = { 0xFFFF, 0xF0F0, 0xAAAA, 0xFF18 };
        const int idx = (style < 0 || style > 3) ? 1 : style;
        _stylePath->linePattern.setValue(kPat[idx]);
    }
    rebuildPath();
}

void AxisFlowCompass::rebuildPath()
{
    if (!_pathSwitch)
        return;

    auto hide = [this]() {
        _linePath->numVertices.setValue(0);
        _pathSwitch->whichChild.setValue(SO_SWITCH_NONE);
    };

    if (!_pathEnabled || !_pathActive) {
        hide();
        return;
    }

    // The shape itself is decided by buildCompassPath() (AxisFlowPath.h, pure
    // and Tier-1 tested). This function only supplies compass-local input and
    // hands the result to Coin.
    //
    // _localEnd is the PCP: the tool feeds the SOLVED point in through
    // setCursorPoint(), so every lock is already baked into it.
    PathInput in;
    in.pcp    = PathPoint { _localEnd[0], _localEnd[1], _localEnd[2] };
    in.lockU  = _pathLockU;
    in.lockV  = _pathLockV;
    in.hasSnap = _pathHasSnap;
    if (_pathHasSnap) {
        // Same local frame as _localEnd: the basis is orthonormal, so a dot
        // product is the inverse transform.
        const SbVec3f d = _pathSnapWorld - _origin;
        in.snap = PathPoint { d.dot(_u), d.dot(_v), d.dot(_n) };
    }

    PathPoint local[kMaxPathPoints];
    const int n = buildCompassPath(in, local);
    if (n < 2) {                 // everything collapsed onto the centre
        hide();
        return;
    }

    SbVec3f pts[kMaxPathPoints];
    for (int i = 0; i < n; ++i)
        pts[i] = SbVec3f(float(local[i].x), float(local[i].y), float(local[i].z));

    _coordPath->point.setValues(0, n, pts);
    _coordPath->point.setNum(n);
    _linePath->numVertices.setValue(n);
    _pathSwitch->whichChild.setValue(0);
}

void AxisFlowCompass::setIndexingCandidates(bool distanceActive, bool axisActive)
{
    if (distanceActive == _distCandActive && axisActive == _axisCandActive)
        return;
    _distCandActive = distanceActive;
    _axisCandActive = axisActive;
    rebuildGuide();
}

// ---------------------------------------------------------------------------

void AxisFlowCompass::setOrigin(const SbVec3f& pt)
{
    _origin = pt;
    _transform->translation.setValue(pt);
    rebuildGuide();
}

AxisFlowCompass::~AxisFlowCompass()
{
    if (_animSensor) {
        _animSensor->unschedule();
        delete _animSensor;
        _animSensor = nullptr;
    }
    // The constructor ref'd the root; without the matching unref the whole
    // compass subgraph outlived every session, one leaked copy per tool.
    // The scenegraph holds its own reference until deactivate() removes it,
    // so this only ever drops OUR reference.
    if (_root) {
        _root->unref();
        _root = nullptr;
    }
}

void AxisFlowCompass::setPlane(const SbVec3f& u, const SbVec3f& v, const SbVec3f& n)
{
    // Rows of the rotation matrix are the plane axes (Coin's row-vector
    // convention): local X -> u, Y -> v, Z -> n.
    SbMatrix m;
    m.makeIdentity();
    m[0][0] = u[0]; m[0][1] = u[1]; m[0][2] = u[2];
    m[1][0] = v[0]; m[1][1] = v[1]; m[1][2] = v[2];
    m[2][0] = n[0]; m[2][1] = n[1]; m[2][2] = n[2];
    const SbRotation target(m);

    _animTgtU = u; _animTgtV = v; _animTgtN = n;

    // Snap when: animation disabled, first orientation ever (activation must
    // not sweep in from identity), or the change is negligible.
    const SbRotation current = _transform->rotation.getValue();
    SbVec3f probe(1, 0, 0), probeTgt(1, 0, 0);
    current.multVec(probe, probe);
    target.multVec(probeTgt, probeTgt);
    const bool negligible = (probe - probeTgt).length() < 1e-6f
                         && std::fabs(_n.dot(n) - 1.0f) < 1e-6f;
    if (_animDurationS <= 0.0f || !_hasOrientation || negligible) {
        if (_animSensor) _animSensor->unschedule();
        _hasOrientation = true;
        applyRotationNow(target);
        _u = u; _v = v; _n = n;   // exact basis, no numeric drift
        rebuildGuide();
        return;
    }

    // Retargeting mid-flight starts from the CURRENT interpolated pose, so a
    // quick T-F-S sequence chains smoothly instead of jumping.
    _animFrom = current;
    _animTo   = target;
    _animT0   = SbTime::getTimeOfDay();
    if (!_animSensor) {
        _animSensor = new SoTimerSensor(&AxisFlowCompass::animTickCB, this);
        _animSensor->setInterval(SbTime(1.0 / 60.0));
    }
    if (!_animSensor->isScheduled())
        _animSensor->schedule();
}

void AxisFlowCompass::animTickCB(void* data, SoSensor*)
{
    static_cast<AxisFlowCompass*>(data)->animTick();
}

void AxisFlowCompass::animTick()
{
    const double t = (SbTime::getTimeOfDay() - _animT0).getValue()
                   / double(_animDurationS);
    if (t >= 1.0) {
        finishAnimation();
        return;
    }
    const float e = float(t * t * (3.0 - 2.0 * t));   // smoothstep ease in/out
    applyRotationNow(SbRotation::slerp(_animFrom, _animTo, e));
    rebuildGuide();
}

void AxisFlowCompass::finishAnimation()
{
    if (_animSensor)
        _animSensor->unschedule();
    applyRotationNow(_animTo);
    _u = _animTgtU; _v = _animTgtV; _n = _animTgtN;   // land exactly
    rebuildGuide();
}

void AxisFlowCompass::applyRotationNow(const SbRotation& r)
{
    _transform->rotation.setValue(r);
    // Keep the guide-line math in lockstep with what is on screen: derive the
    // display basis from the very same rotation (rows = axis images).
    SbVec3f u(1, 0, 0), v(0, 1, 0), n(0, 0, 1);
    r.multVec(u, _u);
    r.multVec(v, _v);
    r.multVec(n, _n);
}

void AxisFlowCompass::setCursorPoint(const SbVec3f& pt)
{
    _cursor = pt;
    rebuildGuide();
}

void AxisFlowCompass::setRadius(float worldRadius)
{
    // A degenerate radius collapses the contour and both axes into the centre
    // point -- which still renders, because points are sized in pixels. That
    // looks exactly like "the compass disappeared", so refuse the value instead.
    if (!(worldRadius > 1e-4f) || !std::isfinite(worldRadius))
        return;
    if (std::fabs(worldRadius - _radius) < 1e-6f)
        return;
    _radius = worldRadius;
    rebuild();
}

void AxisFlowCompass::setVisible(bool on)
{
    _visible = on;
    _visSwitch->whichChild.setValue(on ? 0 : SO_SWITCH_NONE);
}

void AxisFlowCompass::setMode(CompassMode mode)
{
    // Ortho -> square contour, radial -> circle. Remembered even while hidden.
    _mode = (mode == CompassMode::Polar) ? 1 : 0;
    _contour->whichChild.setValue(_mode);
}

} // namespace AxisFlowGui
