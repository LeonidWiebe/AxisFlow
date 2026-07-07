#include "PreCompiled.h"

#include "AxisFlowCompass.h"
#include "AxisFlowSolver.h"   // for CompassMode enum

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

namespace AxisFlowGui {

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static SoSeparator* makeAxisNode(
    SoBaseColor*&   outColor,
    SoCoordinate3*& outCoord,
    SoLineSet*&     outLine,
    SoTranslation*& outLabelTrans,
    SoText2*&       outLabel,
    const SbColor&  color,
    const SbVec3f&  dir,
    float           len,
    const char*     labelText)
{
    auto* sep = new SoSeparator;

    outColor = new SoBaseColor;
    outColor->rgb.setValue(color);

    auto* style = new SoDrawStyle;
    style->lineWidth.setValue(2.0f);

    SbVec3f pts[2] = { SbVec3f(0,0,0), dir * len };

    outCoord = new SoCoordinate3;
    outCoord->point.setValues(0, 2, pts);

    outLine = new SoLineSet;
    outLine->numVertices.setValue(2);

    // Label near the tip
    outLabelTrans = new SoTranslation;
    outLabelTrans->translation.setValue(dir * (len * 1.1f));

    outLabel = new SoText2;
    outLabel->string.setValue(labelText);

    sep->addChild(outColor);
    sep->addChild(style);
    sep->addChild(outCoord);
    sep->addChild(outLine);
    sep->addChild(outLabelTrans);
    sep->addChild(outLabel);

    return sep;
}

// ---------------------------------------------------------------------------
// AxisFlowCompass
// ---------------------------------------------------------------------------

AxisFlowCompass::AxisFlowCompass()
{
    build();
}

void AxisFlowCompass::build()
{
    _root = new SoSeparator;
    _root->ref();   // keep alive

    // Master transform — positions/rotates the whole compass
    _transform = new SoTransform;
    _root->addChild(_transform);

    // ---- Axes ----
    SoSeparator* axisX = makeAxisNode(
        _colorX, _coordX, _lineX, _transLabelX, _labelX,
        SbColor(1,0,0), SbVec3f(1,0,0), _axisLen, "X");

    SoSeparator* axisY = makeAxisNode(
        _colorY, _coordY, _lineY, _transLabelY, _labelY,
        SbColor(0,0.8f,0), SbVec3f(0,1,0), _axisLen, "Y");

    SoSeparator* axisZ = makeAxisNode(
        _colorZ, _coordZ, _lineZ, _transLabelZ, _labelZ,
        SbColor(0.2f,0.4f,1), SbVec3f(0,0,1), _axisLen, "Z");

    _root->addChild(axisX);
    _root->addChild(axisY);
    _root->addChild(axisZ);

    // ---- Origin dot ----
    {
        auto* dotSep = new SoSeparator;

        _colorDot = new SoBaseColor;
        _colorDot->rgb.setValue(1, 1, 1);

        _dotStyle = new SoDrawStyle;
        _dotStyle->pointSize.setValue(6.0f);

        _coordDot = new SoCoordinate3;
        SbVec3f origin(0,0,0);
        _coordDot->point.setValue(origin);

        _dot = new SoPointSet;

        dotSep->addChild(_colorDot);
        dotSep->addChild(_dotStyle);
        dotSep->addChild(_coordDot);
        dotSep->addChild(_dot);

        _root->addChild(dotSep);
    }

    // ---- Guide line (cursor → origin, yellow) ----
    {
        _guideSep = new SoSeparator;

        _colorGuide = new SoBaseColor;
        _colorGuide->rgb.setValue(1, 1, 0);

        auto* guideStyle = new SoDrawStyle;
        guideStyle->lineWidth.setValue(1.5f);

        _coordGuide = new SoCoordinate3;
        SbVec3f pts[2] = { SbVec3f(0,0,0), SbVec3f(0,0,0) };
        _coordGuide->point.setValues(0, 2, pts);

        _lineGuide = new SoLineSet;
        _lineGuide->numVertices.setValue(2);

        _guideSep->addChild(_colorGuide);
        _guideSep->addChild(guideStyle);
        _guideSep->addChild(_coordGuide);
        _guideSep->addChild(_lineGuide);

        _root->addChild(_guideSep);
    }
}

void AxisFlowCompass::setOrigin(const SbVec3f& pt)
{
    _origin = pt;
    _transform->translation.setValue(pt);
    rebuildGuide();
}

void AxisFlowCompass::setRotation(const SbRotation& rot)
{
    _transform->rotation.setValue(rot);
}

void AxisFlowCompass::setCursorPoint(const SbVec3f& pt)
{
    _cursor = pt;
    rebuildGuide();
}

void AxisFlowCompass::setAxisLength(float len)
{
    _axisLen = len;
    rebuildAxes();
}

void AxisFlowCompass::setVisible(bool on)
{
    // SoSeparator has no single visibility flag; we use whichChild on a Switch,
    // but for simplicity here we just clear/add children count.
    // Easiest: store a SoSwitch at root level.
    // For now: scale to zero when hidden.
    _transform->scaleFactor.setValue(on ? SbVec3f(1,1,1) : SbVec3f(0,0,0));
}

void AxisFlowCompass::setMode(CompassMode mode)
{
    if (mode == CompassMode::Polar) {
        _labelX->string.setValue("D");   // Distance
        _labelY->string.setValue("A");   // Angle
    } else {
        _labelX->string.setValue("X");
        _labelY->string.setValue("Y");
    }
}

// ---------------------------------------------------------------------------
// private helpers
// ---------------------------------------------------------------------------

void AxisFlowCompass::rebuildAxes()
{
    auto updateAxis = [&](SoCoordinate3* coord,
                          SoTranslation* labelTrans,
                          const SbVec3f& dir)
    {
        SbVec3f pts[2] = { SbVec3f(0,0,0), dir * _axisLen };
        coord->point.setValues(0, 2, pts);
        labelTrans->translation.setValue(dir * (_axisLen * 1.1f));
    };

    updateAxis(_coordX, _transLabelX, SbVec3f(1,0,0));
    updateAxis(_coordY, _transLabelY, SbVec3f(0,1,0));
    updateAxis(_coordZ, _transLabelZ, SbVec3f(0,0,1));
}

void AxisFlowCompass::rebuildGuide()
{
    // The compass subgraph is positioned at _origin via _transform,
    // so the guide coords must be in compass-LOCAL space.
    // Local origin = (0,0,0), local cursor = world cursor − world origin.
    SbVec3f localCursor = _cursor - _origin;

    SbVec3f pts[2] = { SbVec3f(0,0,0), localCursor };
    _coordGuide->point.setValues(0, 2, pts);
}

} // namespace AxisFlowGui
