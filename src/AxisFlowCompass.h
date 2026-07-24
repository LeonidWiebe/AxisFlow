#pragma once

#include <Inventor/SbVec3f.h>
#include <Inventor/SbRotation.h>
#include <Inventor/SbTime.h>
#include <Inventor/SbColor.h>

class SoSeparator;
class SoSwitch;
class SoTransform;
class SoCoordinate3;
class SoLineSet;
class SoFaceSet;
class SoPointSet;
class SoBaseColor;
class SoMaterial;
class SoDrawStyle;
class SoTimerSensor;
class SoSensor;

namespace AxisFlowGui {

enum class CompassMode;

/**
 * AxisFlowCompass
 *
 * Coin3D subgraph for the AccuDraw-style compass:
 *
 *   - a CONTOUR with a transparent fill and a crisp centre point;
 *   - the contour is a SQUARE in ortho mode and a CIRCLE in radial mode;
 *   - exactly two axes, X and Y. The Z axis is never drawn -- the compass lies
 *     in the working plane, and a Z line would only read as clutter;
 *   - a guide line from the origin to the current cursor point.
 *
 * Axis colour, thickness, start offset and length come from AxisFlowSettings.
 * Start/length are fractions of the centre-to-contour distance, so they hold at
 * any compass size.
 *
 * Geometry is built in compass-LOCAL space (X = plane u, Y = plane v) and placed
 * by _transform, so setPlane() is what makes the compass lie in the working
 * plane rather than the world XY plane.
 *
 * setRadius() takes a WORLD radius; converting the pixel size from the settings
 * into world units needs the camera and is therefore the caller's job.
 */
class AxisFlowCompass
{
public:
    AxisFlowCompass();
    ~AxisFlowCompass();   // stops the rotation-animation sensor

    SoSeparator* getRootNode() const { return _root; }

    /** Place the compass centre at a world point. */
    void setOrigin(const SbVec3f& pt);

    /** Orient the compass to a working plane (orthonormal basis). Purely a
     *  DISPLAY concern: when the rotation-animation setting is non-zero the
     *  compass eases into the new orientation (slerp, smoothstep) while the
     *  solver has already switched -- input math never waits for the visuals.
     *  The internal u/v/n (used by the guide-line math) are interpolated in
     *  lockstep with the transform, so the live line stays glued to the
     *  cursor throughout. */
    void setPlane(const SbVec3f& u, const SbVec3f& v, const SbVec3f& n);

    /** Guide line endpoint, in world space. */
    void setCursorPoint(const SbVec3f& pt);

    /** Style the live guide line: on/off, pattern (0 solid,1 dash,2 dot,3 dash-dot),
     *  colour 0xRRGGBBAA, transparency 0 opaque .. 1 invisible. */
    void applyGuideStyle(bool enabled, int style, unsigned long rgba, double transparency);

    /** Style the candidate-axis line (own weight + transparency; colour and
     *  pattern are inherited from the live line via applySettings()). */
    void applyCandidateAxisStyle(int weightPx, double transparency);

    /**
     * Compass path: the dashed breakdown of how the prospective click point
     * (PCP -- the point that would be placed right now, locks and all) is
     * reached from the compass centre, continuing to the snap point when the
     * two differ. Shown while an object snap is live or any coordinate field
     * is frozen, i.e. exactly when the point is no longer simply "under the
     * cursor" and the reading benefits from being spelled out.
     *
     * The shape depends on which in-plane fields are frozen (@p lockU, @p
     * lockV -- the panel's check boxes):
     *
     *   nothing frozen   centre -> PCP projected on the plane -> PCP
     *   U frozen         centre -> (u, 0) along U -> plane projection -> PCP
     *   V frozen         centre -> (0, v) along V -> plane projection -> PCP
     *   both frozen      centre -> (u, 0) -> (u, v) -> PCP
     *
     * A frozen normal component (Z) does not change the shape, it only fixes
     * the length of the last leg. When the snap point differs from the PCP --
     * which happens exactly when a lock overrides part of the snapped point --
     * one more leg runs from the PCP to it.
     *
     * The path does NOT replace the candidate-axis line: that one is
     * axis-indexing feedback ("you are riding this axis") and stays visible
     * whenever indexing fires. The two answer different questions.
     */
    void setPathState(bool active, bool lockU, bool lockV,
                      bool hasSnap, const SbVec3f& snapWorld);
    bool pathActive() const { return _pathActive; }

    /** Style the compass path: pattern (0 solid,1 dash,2 dot,3 dash-dot) and
     *  colour 0xRRGGBBAA. The width is deliberately fixed at one pixel -- the
     *  path is a read-out, not a highlight. */
    void applyPathStyle(bool enabled, int style, unsigned long rgba);

    /** Drive the indexing markers computed by the solver:
     *   distanceActive -> show the candidate point at the live-line end;
     *   axisActive     -> hide the live line and show the candidate-axis line.
     */
    void setIndexingCandidates(bool distanceActive, bool axisActive);

    /** Centre-to-contour distance, in world units. */
    void setRadius(float worldRadius);
    float radius() const { return _radius; }

    void setVisible(bool on);
    bool visible() const { return _visible; }

    /** Ortho -> square contour, radial -> circle contour. */
    void setMode(CompassMode mode);

    /** Re-read colours, thickness and axis fractions from AxisFlowSettings. */
    void applySettings();

private:
    void build();
    void rebuild();          // contour + axes + dot, from _radius and settings
    void rebuildGuide();     // origin -> cursor, in local space
    void rebuildPath();      // centre -> in-plane projection -> point

    SoSeparator*   _root       { nullptr };
    SoSwitch*      _visSwitch  { nullptr };   // whole compass on/off
    SoTransform*   _transform  { nullptr };

    // Contour: switch between square (0) and circle (1).
    SoSwitch*      _contour    { nullptr };
    SoMaterial*    _fillSq     { nullptr };
    SoCoordinate3* _coordSq    { nullptr };
    SoFaceSet*     _faceSq     { nullptr };
    SoCoordinate3* _outlineSq  { nullptr };
    SoLineSet*     _lineSq     { nullptr };
    SoDrawStyle*   _styleSq    { nullptr };

    SoMaterial*    _fillCi     { nullptr };
    SoCoordinate3* _coordCi    { nullptr };
    SoFaceSet*     _faceCi     { nullptr };
    SoCoordinate3* _outlineCi  { nullptr };
    SoLineSet*     _lineCi     { nullptr };
    SoDrawStyle*   _styleCi    { nullptr };

    // Centre point.
    SoBaseColor*   _dotColor   { nullptr };
    SoDrawStyle*   _dotStyle   { nullptr };
    SoCoordinate3* _coordDot   { nullptr };
    SoPointSet*    _dot        { nullptr };

    // Axes (X, Y only).
    SoBaseColor*   _colorX     { nullptr };
    SoDrawStyle*   _styleX     { nullptr };
    SoCoordinate3* _coordX     { nullptr };
    SoLineSet*     _lineX      { nullptr };

    SoBaseColor*   _colorY     { nullptr };
    SoDrawStyle*   _styleY     { nullptr };
    SoCoordinate3* _coordY     { nullptr };
    SoLineSet*     _lineY      { nullptr };

    // Guide line (live line).
    SoSeparator*   _guideSep   { nullptr };
    SoMaterial*    _matGuide   { nullptr };   // colour + transparency
    SoCoordinate3* _coordGuide { nullptr };
    SoLineSet*     _lineGuide  { nullptr };
    SoDrawStyle*   _styleGuide { nullptr };
    bool           _guideEnabled { true };

    // Candidate axis: replaces the live line while axis-indexing snaps. Same
    // colour + pattern as the live line, own weight + transparency.
    SoSwitch*      _axisCandSwitch { nullptr };
    SoMaterial*    _matAxisCand    { nullptr };
    SoDrawStyle*   _styleAxisCand  { nullptr };
    SoCoordinate3* _coordAxisCand  { nullptr };
    SoLineSet*     _lineAxisCand   { nullptr };

    // Compass path: centre -> (lx,ly,0) -> (lx,ly,lz), one thin dashed polyline.
    SoSwitch*      _pathSwitch { nullptr };
    SoMaterial*    _matPath    { nullptr };
    SoDrawStyle*   _stylePath  { nullptr };
    SoCoordinate3* _coordPath  { nullptr };
    SoLineSet*     _linePath   { nullptr };
    bool           _pathActive  { false };   // snap live / a field frozen
    bool           _pathEnabled { true };    // from settings
    bool           _pathLockU   { false };   // in-plane U field frozen
    bool           _pathLockV   { false };   // in-plane V field frozen
    bool           _pathHasSnap { false };
    SbVec3f        _pathSnapWorld { 0, 0, 0 };

    // Candidate point: a prominent dot at the live-line end, shown while
    // distance-indexing snaps. Same size as the centre dot.
    SoSwitch*      _candDotSwitch { nullptr };
    SoBaseColor*   _candDotColor  { nullptr };
    SoDrawStyle*   _candDotStyle  { nullptr };
    SoCoordinate3* _candDotCoord  { nullptr };
    SoPointSet*    _candDot       { nullptr };

    bool           _distCandActive { false };
    bool           _axisCandActive { false };
    SbVec3f        _localEnd { 0, 0, 0 };   // live-line end, compass-local

    float   _radius  { 50.0f };
    bool    _visible { true };
    int     _mode    { 0 };   // set via setMode(); 0 = ortho, 1 = radial
    SbVec3f _origin  { 0, 0, 0 };
    SbVec3f _cursor  { 0, 0, 0 };

    // ---- rotation animation (display only) --------------------------------
    SoTimerSensor* _animSensor { nullptr };
    SbRotation _animFrom, _animTo;
    SbVec3f    _animTgtU { 1, 0, 0 }, _animTgtV { 0, 1, 0 }, _animTgtN { 0, 0, 1 };
    SbTime     _animT0;
    float      _animDurationS { 0.5f };   // 0 = instant (from settings)
    bool       _hasOrientation { false }; // first setPlane snaps, no animation
    static void animTickCB(void* data, SoSensor*);
    void animTick();
    void applyRotationNow(const SbRotation& r);  // transform + derived u/v/n
    void finishAnimation();
    SbVec3f _u       { 1, 0, 0 };
    SbVec3f _v       { 0, 1, 0 };
    SbVec3f _n       { 0, 0, 1 };
};

} // namespace AxisFlowGui
