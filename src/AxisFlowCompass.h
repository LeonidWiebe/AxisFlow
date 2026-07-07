#pragma once

#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoTransform.h>
#include <Inventor/nodes/SoCoordinate3.h>
#include <Inventor/nodes/SoLineSet.h>
#include <Inventor/nodes/SoPointSet.h>
#include <Inventor/nodes/SoText2.h>
#include <Inventor/nodes/SoTranslation.h>
#include <Inventor/nodes/SoBaseColor.h>
#include <Inventor/nodes/SoDrawStyle.h>
#include <Inventor/SbVec3f.h>
#include <Inventor/SbRotation.h>

namespace AxisFlowGui {

enum class CompassMode;

/**
 * AxisFlowCompass
 *
 * Builds and owns a Coin3D subgraph that renders:
 *  - Three coloured axis lines  (red=X, green=Y, blue=Z)
 *  - A small origin dot
 *  - Axis labels X / Y / Z
 *  - A dashed "snap" guide line from origin to current cursor point
 *
 * Usage:
 *   AxisFlowCompass compass;
 *   sceneRoot->addChild(compass.getRootNode());
 *   compass.setOrigin(SbVec3f(x,y,z));
 *   compass.setCursorPoint(SbVec3f(cx,cy,cz));
 *   compass.setRotation(SbRotation(...));
 */
class AxisFlowCompass
{
public:
    AxisFlowCompass();
    ~AxisFlowCompass() = default;

    /** Returns the Coin3D node to insert into the scene graph. */
    SoSeparator* getRootNode() const { return _root; }

    /** Move compass origin to world-space point. */
    void setOrigin(const SbVec3f& pt);

    /** Rotate compass axes (e.g. when aligning to an edge). */
    void setRotation(const SbRotation& rot);

    /** Highlight the guide line from origin to cursor. */
    void setCursorPoint(const SbVec3f& pt);

    /** Adjust axis length (default 50 mm). */
    void setAxisLength(float len);

    /** Show/hide the whole compass. */
    void setVisible(bool on);

    /** Switch label style (rectangular ↔ polar). */
    void setMode(CompassMode mode);

private:
    void build();
    void rebuildAxes();
    void rebuildGuide();

    SoSeparator*    _root;

    // Transform positions the compass at the origin point
    SoTransform*    _transform;

    // ---- Axis X (red) ----
    SoBaseColor*    _colorX;
    SoCoordinate3*  _coordX;
    SoLineSet*      _lineX;
    SoTranslation*  _transLabelX;
    SoText2*        _labelX;

    // ---- Axis Y (green) ----
    SoBaseColor*    _colorY;
    SoCoordinate3*  _coordY;
    SoLineSet*      _lineY;
    SoTranslation*  _transLabelY;
    SoText2*        _labelY;

    // ---- Axis Z (blue) ----
    SoBaseColor*    _colorZ;
    SoCoordinate3*  _coordZ;
    SoLineSet*      _lineZ;
    SoTranslation*  _transLabelZ;
    SoText2*        _labelZ;

    // ---- Origin dot (white) ----
    SoBaseColor*    _colorDot;
    SoCoordinate3*  _coordDot;
    SoDrawStyle*    _dotStyle;
    SoPointSet*     _dot;

    // ---- Guide line (yellow, dashed) ----
    SoSeparator*    _guideSep;
    SoBaseColor*    _colorGuide;
    SoCoordinate3*  _coordGuide;
    SoLineSet*      _lineGuide;

    float           _axisLen { 50.f };
    SbVec3f         _origin  { 0, 0, 0 };
    SbVec3f         _cursor  { 0, 0, 0 };
};

} // namespace AxisFlowGui
