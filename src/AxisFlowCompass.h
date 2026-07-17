#pragma once

#include <Inventor/SbVec3f.h>
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
    ~AxisFlowCompass() = default;

    SoSeparator* getRootNode() const { return _root; }

    /** Place the compass centre at a world point. */
    void setOrigin(const SbVec3f& pt);

    /** Orient the compass to a working plane (orthonormal basis). */
    void setPlane(const SbVec3f& u, const SbVec3f& v, const SbVec3f& n);

    /** Guide line endpoint, in world space. */
    void setCursorPoint(const SbVec3f& pt);

    /** Style the live guide line: on/off, pattern (0 solid,1 dash,2 dot,3 dash-dot),
     *  colour 0xRRGGBBAA, transparency 0 opaque .. 1 invisible. */
    void applyGuideStyle(bool enabled, int style, unsigned long rgba, double transparency);

    /** Style the candidate-axis line (own weight + transparency; colour and
     *  pattern are inherited from the live line via applySettings()). */
    void applyCandidateAxisStyle(int weightPx, double transparency);

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
    SbVec3f _u       { 1, 0, 0 };
    SbVec3f _v       { 0, 1, 0 };
    SbVec3f _n       { 0, 0, 1 };
};

} // namespace AxisFlowGui
