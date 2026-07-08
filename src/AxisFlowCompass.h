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

    // Guide line.
    SoSeparator*   _guideSep   { nullptr };
    SoBaseColor*   _colorGuide { nullptr };
    SoCoordinate3* _coordGuide { nullptr };
    SoLineSet*     _lineGuide  { nullptr };

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
