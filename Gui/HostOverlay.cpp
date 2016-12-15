/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of Natron <http://www.natron.fr/>,
 * Copyright (C) 2016 INRIA and Alexandre Gauthier-Foichat
 *
 * Natron is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Natron is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Natron.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

// ***** BEGIN PYTHON BLOCK *****
// from <https://docs.python.org/3/c-api/intro.html#include-files>:
// "Since Python may define some pre-processor definitions which affect the standard headers on some systems, you must include Python.h before any standard headers are included."
#include <Python.h>
// ***** END PYTHON BLOCK *****

#include "HostOverlay.h"

#include <list>
#include <cmath>
#include <stdexcept>

#include <boost/weak_ptr.hpp>

CLANG_DIAG_OFF(deprecated)
#include "Global/GLIncludes.h" //!<must be included before QGLWidget
#include <QtOpenGL/QGLWidget>
#include "Global/GLObfuscate.h" //!<must be included after QGLWidget
CLANG_DIAG_ON(deprecated)
#include <QtCore/QPointF>
#include <QtCore/QThread>
#include <QFont>
#include <QColor>
#include <QApplication>

#include "Gui/NodeGui.h"
#include "Gui/TextRenderer.h"
#include "Gui/GuiApplicationManager.h"
#include "Gui/GuiAppInstance.h"

#include "Engine/Curve.h"
#include "Engine/EffectInstance.h"
#include "Engine/Knob.h"
#include "Engine/KnobTypes.h"
#include "Engine/Node.h"
#include "Engine/OSGLFunctions.h"
#include "Engine/Settings.h"
#include "Engine/Transform.h"
#include "Engine/ViewIdx.h"

#include "Global/KeySymbols.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288419717
#endif

NATRON_NAMESPACE_ENTER;

DefaultInteractI::DefaultInteractI(HostOverlay* overlay)
    : _overlay(overlay)
{
}

DefaultInteractI::~DefaultInteractI()
{
}

void
DefaultInteractI::renderText(float x,
                             float y,
                             float scalex,
                             float scaley,
                             const QString &text,
                             const QColor &color,
                             const QFont &font,
                             int flags) const
{
    _overlay->renderText(x, y, scalex, scaley, text, color, font, flags);
}

void
DefaultInteractI::requestRedraw()
{
    _overlay->requestRedraw();
}

void
DefaultInteractI::getPixelScale(double& scaleX,
                                double& scaleY) const
{
    _overlay->n_getPixelScale(scaleX, scaleY);
}

void
DefaultInteractI::draw(double /*time*/,
                       const RenderScale& /*renderScale*/,
                       ViewIdx /*view*/,
                       const OfxPointD& /*pscale*/,
                       const QPointF& /*lastPenPos*/,
                       const OfxRGBColourD& /*color*/,
                       const OfxPointD& /*shadow*/,
                       const QFont& /*font*/,
                       const QFontMetrics& /*fm*/)
{
}

bool
DefaultInteractI::penMotion(double /*time*/,
                            const RenderScale& /*renderScale*/,
                            ViewIdx /*view*/,
                            const OfxPointD& /*pscale*/,
                            const QPointF& /*lastPenPos*/,
                            const QPointF & /*penPos*/,
                            const QPoint & /*penPosViewport*/,
                            double /*pressure*/)
{
    return false;
}

bool
DefaultInteractI::penUp(double /*time*/,
                        const RenderScale& /*renderScale*/,
                        ViewIdx /*view*/,
                        const OfxPointD& /*pscale*/,
                        const QPointF& /*lastPenPos*/,
                        const QPointF & /*penPos*/,
                        const QPoint & /*penPosViewport*/,
                        double /*pressure*/)
{
    return false;
}

bool
DefaultInteractI::penDown(double /*time*/,
                          const RenderScale& /*renderScale*/,
                          ViewIdx /*view*/,
                          const OfxPointD& /*pscale*/,
                          const QPointF& /*lastPenPos*/,
                          const QPointF & /*penPos*/,
                          const QPoint & /*penPosViewport*/,
                          double /*pressure*/)
{
    return false;
}

bool
DefaultInteractI::penDoubleClicked(double /*time*/,
                                   const RenderScale& /*renderScale*/,
                                   ViewIdx /*view*/,
                                   const OfxPointD& /*pscale*/,
                                   const QPointF& /*lastPenPos*/,
                                   const QPointF & /*penPos*/,
                                   const QPoint & /*penPosViewport*/)
{
    return false;
}

bool
DefaultInteractI::keyDown(double /*time*/,
                          const RenderScale& /*renderScale*/,
                          ViewIdx /*view*/,
                          int /*key*/,
                          char*   /*keyString*/)
{
    return false;
}

bool
DefaultInteractI::keyUp(double /*time*/,
                        const RenderScale& /*renderScale*/,
                        ViewIdx /*view*/,
                        int /*key*/,
                        char*   /*keyString*/)
{
    return false;
}

bool
DefaultInteractI::keyRepeat(double /*time*/,
                            const RenderScale& /*renderScale*/,
                            ViewIdx /*view*/,
                            int /*key*/,
                            char*   /*keyString*/)
{
    return false;
}

bool
DefaultInteractI::gainFocus(double /*time*/,
                            const RenderScale& /*renderScale*/,
                            ViewIdx /*view*/)
{
    return false;
}

bool
DefaultInteractI::loseFocus(double /*time*/,
                            const RenderScale& /*renderScale*/,
                            ViewIdx /*view*/)
{
    return false;
}

NATRON_NAMESPACE_ANONYMOUS_ENTER

enum PositionInteractState
{
    ePositionInteractStateInactive,
    ePositionInteractStatePoised,
    ePositionInteractStatePicked
};


class PositionInteract
    : public DefaultInteractI
{
    KnobDoubleWPtr _param;
    KnobBoolWPtr _interactive;
    QPointF _dragPos;
    bool _interactiveDrag;
    PositionInteractState _state;

public:

    PositionInteract(const HostOverlayKnobsPosition* knobs,
                     HostOverlay* overlay)
        : DefaultInteractI(overlay)
        , _param()
        , _interactive()
        , _dragPos()
        , _interactiveDrag(false)
        , _state(ePositionInteractStateInactive)
    {
        _param = knobs->getKnob<KnobDouble>(HostOverlayKnobsPosition::eKnobsEnumerationPosition);
        _interactive = knobs->getKnob<KnobBool>(HostOverlayKnobsPosition::eKnobsEnumerationInteractive);
    }

    virtual ~PositionInteract()
    {
    }

    virtual bool isInteractForKnob(const KnobIConstPtr& knob) const OVERRIDE FINAL
    {
        return _param.lock() == knob;
    }

    double pointSize() const
    {
        return TO_DPIX(5);
    }

    double pointTolerance() const
    {
        return TO_DPIX(12.);
    }

    bool getInteractive(double time) const
    {
        if ( _interactive.lock() ) {
            return _interactive.lock()->getValueAtTime(time);
        } else {
            return !appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly();
        }
    }

    virtual void draw(double time,
                      const RenderScale& renderScale,
                      ViewIdx view,
                      const OfxPointD& pscale,
                      const QPointF& lastPenPos,
                      const OfxRGBColourD& color,
                      const OfxPointD& shadow,
                      const QFont& font,
                      const QFontMetrics& fm) OVERRIDE FINAL;
    virtual bool penMotion(double time,
                           const RenderScale& renderScale,
                           ViewIdx view,
                           const OfxPointD& pscale,
                           const QPointF& lastPenPos,
                           const QPointF &penPos,
                           const QPoint &penPosViewport,
                           double pressure) OVERRIDE FINAL;
    virtual bool penUp(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       const OfxPointD& pscale,
                       const QPointF& lastPenPos,
                       const QPointF &penPos,
                       const QPoint &penPosViewport,
                       double pressure) OVERRIDE FINAL;
    virtual bool penDown(double time,
                         const RenderScale& renderScale,
                         ViewIdx view,
                         const OfxPointD& pscale,
                         const QPointF& lastPenPos,
                         const QPointF &penPos,
                         const QPoint &penPosViewport,
                         double pressure) OVERRIDE FINAL;
    virtual bool loseFocus(double time,
                           const RenderScale& renderScale,
                           ViewIdx view) OVERRIDE FINAL;
};


class TransformInteract
    : public DefaultInteractI
{


    enum DrawStateEnum
    {
        eInActive = 0, //< nothing happening
        eCircleHovered, //< the scale circle is hovered
        eLeftPointHovered, //< the left point of the circle is hovered
        eRightPointHovered, //< the right point of the circle is hovered
        eBottomPointHovered, //< the bottom point of the circle is hovered
        eTopPointHovered, //< the top point of the circle is hovered
        eCenterPointHovered, //< the center point of the circle is hovered
        eRotationBarHovered, //< the rotation bar is hovered
        eSkewXBarHoverered, //< the skew bar is hovered
        eSkewYBarHoverered //< the skew bar is hovered
    };

    enum MouseStateEnum
    {
        eReleased = 0,
        eDraggingCircle,
        eDraggingLeftPoint,
        eDraggingRightPoint,
        eDraggingTopPoint,
        eDraggingBottomPoint,
        eDraggingTranslation,
        eDraggingCenter,
        eDraggingRotationBar,
        eDraggingSkewXBar,
        eDraggingSkewYBar
    };

    enum OrientationEnum
    {
        eOrientationAllDirections = 0,
        eOrientationNotSet,
        eOrientationHorizontal,
        eOrientationVertical
    };

    KnobDoubleWPtr _translate;
    KnobDoubleWPtr _scale;
    KnobBoolWPtr _scaleUniform;
    KnobDoubleWPtr _rotate;
    KnobDoubleWPtr _center;
    KnobDoubleWPtr _skewX;
    KnobDoubleWPtr _skewY;
    KnobChoiceWPtr _skewOrder;
    KnobBoolWPtr _invert;
    KnobBoolWPtr _interactive;
    DrawStateEnum _drawState;
    MouseStateEnum _mouseState;
    int _modifierStateCtrl;
    int _modifierStateShift;
    OrientationEnum _orientation;
    Point _centerDrag;
    Point _translateDrag;
    Point _scaleParamDrag;
    bool _scaleUniformDrag;
    double _rotateDrag;
    double _skewXDrag;
    double _skewYDrag;
    int _skewOrderDrag;
    bool _invertedDrag;
    bool _interactiveDrag;

public:

    TransformInteract(const HostOverlayKnobsTransform* knobs,
                      HostOverlay* overlay)
        : DefaultInteractI(overlay)
        , _translate()
        , _scale()
        , _scaleUniform()
        , _rotate()
        , _center()
        , _skewX()
        , _skewY()
        , _skewOrder()
        , _invert()
        , _interactive()
        , _drawState(eInActive)
        , _mouseState(eReleased)
        , _modifierStateCtrl(0)
        , _modifierStateShift(0)
        , _orientation(eOrientationAllDirections)
        , _centerDrag()
        , _translateDrag()
        , _scaleParamDrag()
        , _scaleUniformDrag(false)
        , _rotateDrag(0)
        , _skewXDrag(0)
        , _skewYDrag(0)
        , _skewOrderDrag(0)
        , _invertedDrag(false)
        , _interactiveDrag(false)
    {
        _translate = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationTranslate);
        _scale = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationScale);
        _scaleUniform = knobs->getKnob<KnobBool>(HostOverlayKnobsTransform::eKnobsEnumerationUniform);
        _rotate = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationRotate);
        _center = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationCenter);
        _skewX = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationSkewx);
        _skewY = knobs->getKnob<KnobDouble>(HostOverlayKnobsTransform::eKnobsEnumerationSkewy);
        _skewOrder = knobs->getKnob<KnobChoice>(HostOverlayKnobsTransform::eKnobsEnumerationSkewOrder);
        _invert = knobs->getKnob<KnobBool>(HostOverlayKnobsTransform::eKnobsEnumerationInvert);
        _interactive = knobs->getKnob<KnobBool>(HostOverlayKnobsTransform::eKnobsEnumerationInteractive);
    }

    virtual ~TransformInteract()
    {
    }

    virtual bool isInteractForKnob(const KnobIConstPtr& knob) const OVERRIDE FINAL
    {
        return knob == _translate.lock() ||
               knob == _scale.lock() ||
               knob == _scaleUniform.lock() ||
               knob == _rotate.lock() ||
               knob == _center.lock() ||
               knob == _skewX.lock() ||
               knob == _skewY.lock() ||
               knob == _skewOrder.lock() ||
               knob == _invert.lock() ||
               knob == _interactive.lock();
    }

    static double circleRadiusBase() { return 30.; }

    static double circleRadiusMin() { return 15.; }

    static double circleRadiusMax() { return 300.; }

    static double pointSize() { return 7.; }

    static double ellipseNPoints() { return 50.; }


    void getTranslate(double time,
                      double* tx,
                      double* ty) const
    {
        KnobDoublePtr knob = _translate.lock();

        assert(knob);
        *tx = knob->getValueAtTime(time, DimIdx(0));
        *ty = knob->getValueAtTime(time, DimIdx(1));
    }

    void getCenter(double time,
                   double* cx,
                   double* cy) const
    {
        KnobDoublePtr knob = _center.lock();

        assert(knob);
        *cx = knob->getValueAtTime(time, DimIdx(0));
        *cy = knob->getValueAtTime(time, DimIdx(1));
    }

    void getScale(double time,
                  double* sx,
                  double* sy) const
    {
        KnobDoublePtr knob = _scale.lock();

        assert(knob);
        *sx = knob->getValueAtTime(time, DimIdx(0));
        *sy = knob->getValueAtTime(time, DimIdx(1));
    }

    void getRotate(double time,
                   double* rot) const
    {
        KnobDoublePtr knob = _rotate.lock();

        assert(knob);
        *rot = knob->getValueAtTime(time, DimIdx(0));
    }

    void getSkewX(double time,
                  double* x) const
    {
        KnobDoublePtr knob = _skewX.lock();

        assert(knob);
        *x = knob->getValueAtTime(time, DimIdx(0));
    }

    void getSkewY(double time,
                  double* y) const
    {
        KnobDoublePtr knob = _skewY.lock();

        assert(knob);
        *y = knob->getValueAtTime(time, DimIdx(0));
    }

    void getSkewOrder(double time,
                      int* order) const
    {
        KnobChoicePtr knob = _skewOrder.lock();

        assert(knob);
        *order = knob->getValueAtTime(time, DimIdx(0));
    }

    void getScaleUniform(double time,
                         bool* uniform) const
    {
        KnobBoolPtr knob = _scaleUniform.lock();

        assert(knob);
        *uniform = knob->getValueAtTime(time, DimIdx(0));
    }

    bool getInvert(double time) const
    {
        KnobBoolPtr knob = _invert.lock();

        if (knob) {
            return knob->getValueAtTime(time);
        } else {
            return false;
        }
    }

    bool getInteractive(double time) const
    {
        if ( _interactive.lock() ) {
            return _interactive.lock()->getValueAtTime(time);
        } else {
            return !appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly();
        }
    }

    virtual void draw(double time,
                      const RenderScale& renderScale,
                      ViewIdx view,
                      const OfxPointD& pscale,
                      const QPointF& lastPenPos,
                      const OfxRGBColourD& color,
                      const OfxPointD& shadow,
                      const QFont& font,
                      const QFontMetrics& fm) OVERRIDE FINAL;
    virtual bool penMotion(double time,
                           const RenderScale& renderScale,
                           ViewIdx view,
                           const OfxPointD& pscale,
                           const QPointF& lastPenPos,
                           const QPointF &penPos,
                           const QPoint &penPosViewport,
                           double pressure) OVERRIDE FINAL;
    virtual bool penUp(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       const OfxPointD& pscale,
                       const QPointF& lastPenPos,
                       const QPointF &penPos,
                       const QPoint &penPosViewport,
                       double pressure) OVERRIDE FINAL;
    virtual bool penDown(double time,
                         const RenderScale& renderScale,
                         ViewIdx view,
                         const OfxPointD& pscale,
                         const QPointF& lastPenPos,
                         const QPointF &penPos,
                         const QPoint &penPosViewport,
                         double pressure) OVERRIDE FINAL;
    virtual bool keyDown(double time,
                         const RenderScale& renderScale,
                         ViewIdx view,
                         int key,
                         char*   keyString) OVERRIDE FINAL;
    virtual bool keyUp(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       int key,
                       char*   keyString) OVERRIDE FINAL;
    virtual bool loseFocus(double time,
                           const RenderScale& renderScale,
                           ViewIdx view) OVERRIDE FINAL;
};

class CornerPinInteract
    : public DefaultInteractI
{
    std::vector<KnobDoubleWPtr > _from, _to;
    std::vector<KnobBoolWPtr > _enable;
    KnobBoolWPtr _invert;
    KnobChoiceWPtr _overlayPoints;
    KnobBoolWPtr _interactive;
    int _dragging; // -1: idle, else dragging point number
    int _hovering; // -1: idle, else hovering point number
    Point _toDrag[4];
    Point _fromDrag[4];
    bool _enableDrag[4];
    bool _useFromDrag;
    bool _interactiveDrag;

public:

    CornerPinInteract(const HostOverlayKnobsCornerPin* knobs,
                      HostOverlay* overlay)
        : DefaultInteractI(overlay)
        , _invert()
        , _interactive()
        , _dragging(-1)
        , _hovering(-1)
        , _useFromDrag(false)
        , _interactiveDrag(false)
    {
        _from.resize(4);
        _to.resize(4);
        _enable.resize(4);

        _from[0] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationFrom1);
        _from[1] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationFrom2);
        _from[2] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationFrom3);
        _from[3] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationFrom4);

        _to[0] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationTo1);
        _to[1] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationTo2);
        _to[2] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationTo3);
        _to[3] = knobs->getKnob<KnobDouble>(HostOverlayKnobsCornerPin::eKnobsEnumerationTo4);


        _enable[0] = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationEnable1);
        _enable[1] = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationEnable2);
        _enable[2] = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationEnable3);
        _enable[3] = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationEnable4);

        _overlayPoints = knobs->getKnob<KnobChoice>(HostOverlayKnobsCornerPin::eKnobsEnumerationOverlayPoints);
        _invert = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationInvert);
        _interactive = knobs->getKnob<KnobBool>(HostOverlayKnobsCornerPin::eKnobsEnumerationInteractive);
        for (unsigned i = 0; i < 4; ++i) {
            _toDrag[i].x = _toDrag[i].y = _fromDrag[i].x = _fromDrag[i].y = 0.;
        }
    }

    virtual ~CornerPinInteract()
    {
    }

    virtual bool isInteractForKnob(const KnobIConstPtr& knob) const OVERRIDE FINAL
    {
        for (int i = 0; i < 4; ++i) {
            if (_from[i].lock() == knob) {
                return true;
            } else if (_to[i].lock() == knob) {
                return true;
            } else if (_enable[i].lock() == knob) {
                return true;
            }
        }
        if (_invert.lock() == knob) {
            return true;
        }
        if (_overlayPoints.lock() == knob) {
            return true;
        }
        if (_interactive.lock() == knob) {
            return true;
        }

        return false;
    }

    static double pointSize() { return TO_DPIX(5.); }

    static double pointTolerance() { return TO_DPIX(12.); }

    static bool isNearby(const QPointF & p,
                         double x,
                         double y,
                         double tolerance,
                         const OfxPointD & pscale)
    {
        return std::fabs(p.x() - x) <= tolerance * pscale.x &&  std::fabs(p.y() - y) <= tolerance * pscale.y;
    }

    void getFrom(double time,
                 int index,
                 double* tx,
                 double* ty) const
    {
        KnobDoublePtr knob = _from[index].lock();

        assert(knob);
        *tx = knob->getValueAtTime(time, DimIdx(0));
        *ty = knob->getValueAtTime(time, DimIdx(1));
    }

    void getTo(double time,
               int index,
               double* tx,
               double* ty) const
    {
        KnobDoublePtr knob = _to[index].lock();

        assert(knob);
        *tx = knob->getValueAtTime(time, DimIdx(0));
        *ty = knob->getValueAtTime(time, DimIdx(1));
    }

    bool getEnabled(double time,
                    int index) const
    {
        KnobBoolPtr knob = _enable[index].lock();

        assert(knob);

        return knob->getValueAtTime(time);
    }

    bool getInverted(double time) const
    {
        KnobBoolPtr knob = _invert.lock();

        assert(knob);

        return knob->getValueAtTime(time);
    }

    bool getUseFromPoints(double time) const
    {
        KnobChoicePtr knob = _overlayPoints.lock();

        assert(knob);

        return knob->getValueAtTime(time) == 1;
    }

    bool getInteractive(double time) const
    {
        if ( _interactive.lock() ) {
            return _interactive.lock()->getValueAtTime(time);
        } else {
            return !appPTR->getCurrentSettings()->getRenderOnEditingFinishedOnly();
        }
    }

    virtual void draw(double time,
                      const RenderScale& renderScale,
                      ViewIdx view,
                      const OfxPointD& pscale,
                      const QPointF& lastPenPos,
                      const OfxRGBColourD& color,
                      const OfxPointD& shadow,
                      const QFont& font,
                      const QFontMetrics& fm) OVERRIDE FINAL;
    virtual bool penMotion(double time,
                           const RenderScale& renderScale,
                           ViewIdx view,
                           const OfxPointD& pscale,
                           const QPointF& lastPenPos,
                           const QPointF &penPos,
                           const QPoint &penPosViewport,
                           double pressure) OVERRIDE FINAL;
    virtual bool penUp(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       const OfxPointD& pscale,
                       const QPointF& lastPenPos,
                       const QPointF &penPos,
                       const QPoint &penPosViewport,
                       double pressure) OVERRIDE FINAL;
    virtual bool penDown(double time,
                         const RenderScale& renderScale,
                         ViewIdx view,
                         const OfxPointD& pscale,
                         const QPointF& lastPenPos,
                         const QPointF &penPos,
                         const QPoint &penPosViewport,
                         double pressure) OVERRIDE FINAL;
    virtual bool loseFocus(double time,
                           const RenderScale& renderScale,
                           ViewIdx view) OVERRIDE FINAL;
};


// round to the closest int, 1/10 int, etc
// this make parameter editing easier
// pscale is args.pixelScale.x / args.renderScale.x;
// pscale10 is the power of 10 below pscale
inline double
fround(double val,
       double pscale)
{
    double pscale10 = std::pow( 10., std::floor( std::log10(pscale) ) );

    return pscale10 * std::floor(val / pscale10 + 0.5);
}

typedef boost::shared_ptr<DefaultInteractI> InteractPtr;
typedef std::list<InteractPtr> InteractList;
typedef std::list<TransformInteract> TransformInteracts;

NATRON_NAMESPACE_ANONYMOUS_EXIT


struct HostOverlayPrivate
{
    HostOverlay* _publicInterface; // can not be a smart ptr
    InteractList interacts;
    boost::weak_ptr<NodeGui> node;
    QPointF lastPenPos;
    TextRenderer textRenderer;
    bool interactiveDrag;

    HostOverlayPrivate(HostOverlay* publicInterface,
                       const NodeGuiPtr& node)
        : _publicInterface(publicInterface)
        , interacts()
        , node(node)
        , lastPenPos()
        , textRenderer()
        , interactiveDrag(false)
    {
    }

    void requestRedraw()
    {
        node.lock()->getNode()->getEffectInstance()->requestOverlayInteractRefresh();
    }
};

void
HostOverlay::renderText(float x,
                        float y,
                        float scalex,
                        float scaley,
                        const QString &text,
                        const QColor &color,
                        const QFont &font,
                        int flags) const
{
    _imp->textRenderer.renderText(x, y, scalex, scaley, text, color, font, flags);
}

void
HostOverlay::requestRedraw()
{
    _imp->requestRedraw();
}

HostOverlay::HostOverlay(const NodeGuiPtr& node)
    : _imp( new HostOverlayPrivate(this, node) )
{
}

HostOverlay::~HostOverlay()
{
}

NodeGuiPtr
HostOverlay::getNode() const
{
    return _imp->node.lock();
}

bool
HostOverlay::addInteract(const HostOverlayKnobsPtr& knobs)
{
    assert( QThread::currentThread() == qApp->thread() );

    KnobIPtr firstKnob = knobs->getFirstKnob();
    if (!firstKnob) {
        return false;
    }
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->isInteractForKnob(firstKnob) ) {
            return false;
        }
    }

    if ( !knobs->checkHostOverlayValid() ) {
        return false;
    }

    HostOverlayKnobsPosition* isPosition = dynamic_cast<HostOverlayKnobsPosition*>( knobs.get() );
    HostOverlayKnobsTransform* isTransform = dynamic_cast<HostOverlayKnobsTransform*>( knobs.get() );
    HostOverlayKnobsCornerPin* isCornerPin = dynamic_cast<HostOverlayKnobsCornerPin*>( knobs.get() );
    boost::shared_ptr<DefaultInteractI> overlay;
    if (isPosition) {
        boost::shared_ptr<PositionInteract> p( new PositionInteract(isPosition, this) );
        overlay = p;
    } else if (isTransform) {
        boost::shared_ptr<TransformInteract> p( new TransformInteract(isTransform, this) );
        overlay = p;
    } else if (isCornerPin) {
        boost::shared_ptr<CornerPinInteract> p( new CornerPinInteract(isCornerPin, this) );
        overlay = p;
    }

    _imp->interacts.push_back(overlay);

    return true;
}

void
PositionInteract::draw(double time,
                       const RenderScale& /*renderScale*/,
                       ViewIdx /*view*/,
                       const OfxPointD& pscale,
                       const QPointF& lastPenPos,
                       const OfxRGBColourD& color,
                       const OfxPointD& shadow,
                       const QFont& font,
                       const QFontMetrics& fm)
{
    KnobDoublePtr knob = _param.lock();

    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    if ( !knob || !knob->shouldDrawOverlayInteract()) {
        return;
    }

    float pR = 1.f;
    float pG = 1.f;
    float pB = 1.f;
    switch (_state) {
    case ePositionInteractStateInactive:
        pR = (float)color.r; pG = (float)color.g; pB = (float)color.b; break;
    case ePositionInteractStatePoised:
        pR = 0.f; pG = 1.0f; pB = 0.0f; break;
    case ePositionInteractStatePicked:
        pR = 0.f; pG = 1.0f; pB = 0.0f; break;
    }

    QPointF pos;
    if (_state == ePositionInteractStatePicked) {
        pos = lastPenPos;
    } else {
        double p[2];
        for (int i = 0; i < 2; ++i) {
            p[i] = knob->getValueAtTime(time, DimIdx(i));
            if (knob->getValueIsNormalized(DimIdx(i)) != eValueIsNormalizedNone) {
                p[i] = knob->denormalize(DimIdx(i), time, p[i]);
            }
        }
        pos.setX(p[0]);
        pos.setY(p[1]);
    }
    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs
    GL_GPU::PointSize( (GLfloat)pointSize() );
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        GL_GPU::MatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        GL_GPU::Translated(direction * shadow.x, -direction * shadow.y, 0);
        GL_GPU::MatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        GL_GPU::Color3f(pR * l, pG * l, pB * l);
        GL_GPU::Begin(GL_POINTS);
        GL_GPU::Vertex2d( pos.x(), pos.y() );
        GL_GPU::End();
        QColor c;
        c.setRgbF(pR * l, pG * l, pB * l);

        renderText(pos.x(), pos.y() - ( fm.height() + pointSize() ) * pscale.y,
                   pscale.x, pscale.y, QString::fromUtf8( knob->getOriginalName().c_str() ), c, font);
    }
} // PositionInteract::draw

static void
getTargetCenter(const OfxPointD &center,
                const OfxPointD &translate,
                OfxPointD *targetCenter)
{
    targetCenter->x = center.x + translate.x;
    targetCenter->y = center.y + translate.y;
}

static void
getTargetRadius(const OfxPointD& scale,
                const OfxPointD& pixelScale,
                OfxPointD* targetRadius)
{
    targetRadius->x = scale.x * TransformInteract::circleRadiusBase();
    targetRadius->y = scale.y * TransformInteract::circleRadiusBase();
    // don't draw too small. 15 pixels is the limit
    if ( ( std::fabs(targetRadius->x) < TransformInteract::circleRadiusMin() ) && ( std::fabs(targetRadius->y) < TransformInteract::circleRadiusMin() ) ) {
        targetRadius->x = targetRadius->x >= 0 ? TransformInteract::circleRadiusMin() : -TransformInteract::circleRadiusMin();
        targetRadius->y = targetRadius->y >= 0 ? TransformInteract::circleRadiusMin() : -TransformInteract::circleRadiusMin();
    } else if ( ( std::fabs(targetRadius->x) > TransformInteract::circleRadiusMax() ) && ( std::fabs(targetRadius->y) > TransformInteract::circleRadiusMax() ) ) {
        targetRadius->x = targetRadius->x >= 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
        targetRadius->y = targetRadius->y >= 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
    } else {
        if ( std::fabs(targetRadius->x) < TransformInteract::circleRadiusMin() ) {
            if ( (targetRadius->x == 0.) && (targetRadius->y != 0.) ) {
                targetRadius->y = targetRadius->y > 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
            } else {
                targetRadius->y *= std::fabs(TransformInteract::circleRadiusMin() / targetRadius->x);
            }
            targetRadius->x = targetRadius->x >= 0 ? TransformInteract::circleRadiusMin() : -TransformInteract::circleRadiusMin();
        }
        if ( std::fabs(targetRadius->x) > TransformInteract::circleRadiusMax() ) {
            targetRadius->y *= std::fabs(TransformInteract::circleRadiusMax() / targetRadius->x);
            targetRadius->x = targetRadius->x > 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
        }
        if ( std::fabs(targetRadius->y) < TransformInteract::circleRadiusMin() ) {
            if ( (targetRadius->y == 0.) && (targetRadius->x != 0.) ) {
                targetRadius->x = targetRadius->x > 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
            } else {
                targetRadius->x *= std::fabs(TransformInteract::circleRadiusMin() / targetRadius->y);
            }
            targetRadius->y = targetRadius->y >= 0 ? TransformInteract::circleRadiusMin() : -TransformInteract::circleRadiusMin();
        }
        if ( std::fabs(targetRadius->y) > TransformInteract::circleRadiusMax() ) {
            targetRadius->x *= std::fabs(TransformInteract::circleRadiusMax() / targetRadius->x);
            targetRadius->y = targetRadius->y > 0 ? TransformInteract::circleRadiusMax() : -TransformInteract::circleRadiusMax();
        }
    }
    // the circle axes are not aligned with the images axes, so we cannot use the x and y scales separately
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    targetRadius->x *= meanPixelScale;
    targetRadius->y *= meanPixelScale;
}

static void
getTargetPoints(const OfxPointD& targetCenter,
                const OfxPointD& targetRadius,
                OfxPointD *left,
                OfxPointD *bottom,
                OfxPointD *top,
                OfxPointD *right)
{
    left->x = targetCenter.x - targetRadius.x;
    left->y = targetCenter.y;
    right->x = targetCenter.x + targetRadius.x;
    right->y = targetCenter.y;
    top->x = targetCenter.x;
    top->y = targetCenter.y + targetRadius.y;
    bottom->x = targetCenter.x;
    bottom->y = targetCenter.y - targetRadius.y;
}

static void
ofxsTransformGetScale(const OfxPointD &scaleParam,
                      bool scaleUniform,
                      OfxPointD* scale)
{
    const double SCALE_MIN = 0.0001;

    scale->x = scaleParam.x;
    if (std::fabs(scale->x) < SCALE_MIN) {
        scale->x = (scale->x >= 0) ? SCALE_MIN : -SCALE_MIN;
    }
    if (scaleUniform) {
        scale->y = scaleParam.x;
    } else {
        scale->y = scaleParam.y;
    }
    if (std::fabs(scale->y) < SCALE_MIN) {
        scale->y = (scale->y >= 0) ? SCALE_MIN : -SCALE_MIN;
    }
}

static void
drawSquare(const OfxRGBColourD& color,
           const OfxPointD& center,
           const OfxPointD& pixelScale,
           bool hovered,
           bool althovered,
           int l)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;

    if (hovered) {
        if (althovered) {
            GL_GPU::Color3f(0.f * l, 1.f * l, 0.f * l);
        } else {
            GL_GPU::Color3f(1.f * l, 0.f * l, 0.f * l);
        }
    } else {
        GL_GPU::Color3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
    }
    double halfWidth = (TransformInteract::pointSize() / 2.) * meanPixelScale;
    double halfHeight = (TransformInteract::pointSize() / 2.) * meanPixelScale;
    GL_GPU::PushMatrix();
    GL_GPU::Translated(center.x, center.y, 0.);
    GL_GPU::Begin(GL_POLYGON);
    GL_GPU::Vertex2d(-halfWidth, -halfHeight);   // bottom left
    GL_GPU::Vertex2d(-halfWidth, +halfHeight);   // top left
    GL_GPU::Vertex2d(+halfWidth, +halfHeight);   // bottom right
    GL_GPU::Vertex2d(+halfWidth, -halfHeight);   // top right
    GL_GPU::End();
    GL_GPU::PopMatrix();
}

static void
drawEllipse(const OfxRGBColourD& color,
            const OfxPointD& center,
            const OfxPointD& targetRadius,
            bool hovered,
            int l)
{
    if (hovered) {
        GL_GPU::Color3f(1.f * l, 0.f * l, 0.f * l);
    } else {
        GL_GPU::Color3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
    }

    GL_GPU::PushMatrix();
    //  center the oval at x_center, y_center
    GL_GPU::Translatef( (float)center.x, (float)center.y, 0.f );
    //  draw the oval using line segments
    GL_GPU::Begin(GL_LINE_LOOP);
    // we don't need to be pixel-perfect here, it's just an interact!
    // 40 segments is enough.
    for (int i = 0; i < 40; ++i) {
        double theta = i * 2 * M_PI / 40.;
        GL_GPU::Vertex2d( targetRadius.x * std::cos(theta), targetRadius.y * std::sin(theta) );
    }
    GL_GPU::End();

    GL_GPU::PopMatrix();
}

static void
drawSkewBar(const OfxRGBColourD& color,
            const OfxPointD &center,
            const OfxPointD& pixelScale,
            double targetRadiusY,
            bool hovered,
            double angle,
            int l)
{
    if (hovered) {
        GL_GPU::Color3f(1.f * l, 0.f * l, 0.f * l);
    } else {
        GL_GPU::Color3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
    }

    // we are not axis-aligned: use the mean pixel scale
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusY + 20. * meanPixelScale;

    GL_GPU::PushMatrix();
    GL_GPU::Translatef( (float)center.x, (float)center.y, 0.f );
    GL_GPU::Rotated(angle, 0, 0, 1);

    GL_GPU::Begin(GL_LINES);
    GL_GPU::Vertex2d(0., -barHalfSize);
    GL_GPU::Vertex2d(0., +barHalfSize);

    if (hovered) {
        double arrowYPosition = targetRadiusY + 10. * meanPixelScale;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        ///draw the central bar
        GL_GPU::Vertex2d(-arrowXHalfSize, -arrowYPosition);
        GL_GPU::Vertex2d(+arrowXHalfSize, -arrowYPosition);

        ///left triangle
        GL_GPU::Vertex2d(-arrowXHalfSize, -arrowYPosition);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, -arrowYPosition + arrowHeadOffsetY);

        GL_GPU::Vertex2d(-arrowXHalfSize, -arrowYPosition);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, -arrowYPosition - arrowHeadOffsetY);

        ///right triangle
        GL_GPU::Vertex2d(+arrowXHalfSize, -arrowYPosition);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, -arrowYPosition + arrowHeadOffsetY);

        GL_GPU::Vertex2d(+arrowXHalfSize, -arrowYPosition);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, -arrowYPosition - arrowHeadOffsetY);
    }
    GL_GPU::End();
    GL_GPU::PopMatrix();
}

static void
drawRotationBar(const OfxRGBColourD& color,
                const OfxPointD& pixelScale,
                double targetRadiusX,
                bool hovered,
                bool inverted,
                int l)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;

    if (hovered) {
        GL_GPU::Color3f(1.f * l, 0.f * l, 0.f * l);
    } else {
        GL_GPU::Color3f(color.r * l, color.g * l, color.b * l);
    }

    double barExtra = 30. * meanPixelScale;
    GL_GPU::Begin(GL_LINES);
    GL_GPU::Vertex2d(0., 0.);
    GL_GPU::Vertex2d(0. + targetRadiusX + barExtra, 0.);
    GL_GPU::End();

    if (hovered) {
        double arrowCenterX = targetRadiusX + barExtra / 2.;

        ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
        OfxPointD arrowRadius;
        arrowRadius.x = 5. * meanPixelScale;
        arrowRadius.y = 10. * meanPixelScale;

        GL_GPU::PushMatrix();
        //  center the oval at x_center, y_center
        GL_GPU::Translatef( (float)arrowCenterX, 0.f, 0 );
        //  draw the oval using line segments
        GL_GPU::Begin(GL_LINE_STRIP);
        GL_GPU::Vertex2d(0, arrowRadius.y);
        GL_GPU::Vertex2d(arrowRadius.x, 0.);
        GL_GPU::Vertex2d(0, -arrowRadius.y);
        GL_GPU::End();


        GL_GPU::Begin(GL_LINES);
        ///draw the top head
        GL_GPU::Vertex2d(0., arrowRadius.y);
        GL_GPU::Vertex2d(0., arrowRadius.y - 5. * meanPixelScale);

        GL_GPU::Vertex2d(0., arrowRadius.y);
        GL_GPU::Vertex2d(4. * meanPixelScale, arrowRadius.y - 3. * meanPixelScale); // 5^2 = 3^2+4^2

        ///draw the bottom head
        GL_GPU::Vertex2d(0., -arrowRadius.y);
        GL_GPU::Vertex2d(0., -arrowRadius.y + 5. * meanPixelScale);

        GL_GPU::Vertex2d(0., -arrowRadius.y);
        GL_GPU::Vertex2d(4. * meanPixelScale, -arrowRadius.y + 3. * meanPixelScale); // 5^2 = 3^2+4^2

        GL_GPU::End();

        GL_GPU::PopMatrix();
    }
    if (inverted) {
        double arrowXPosition = targetRadiusX + barExtra * 1.5;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        GL_GPU::PushMatrix();
        GL_GPU::Translatef( (float)arrowXPosition, 0, 0 );

        GL_GPU::Begin(GL_LINES);
        ///draw the central bar
        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);

        ///left triangle
        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        GL_GPU::End();

        GL_GPU::Rotated(90., 0., 0., 1.);

        GL_GPU::Begin(GL_LINES);
        ///draw the central bar
        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);

        ///left triangle
        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        GL_GPU::Vertex2d(-arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(-arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        GL_GPU::Vertex2d(+arrowXHalfSize, 0.);
        GL_GPU::Vertex2d(+arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        GL_GPU::End();

        GL_GPU::PopMatrix();
    }
} // drawRotationBar

void
TransformInteract::draw(double time,
                        const RenderScale& /*renderScale*/,
                        ViewIdx /*view*/,
                        const OfxPointD& pscale,
                        const QPointF& lastPenPos,
                        const OfxRGBColourD& color,
                        const OfxPointD& shadow,
                        const QFont& /*font*/,
                        const QFontMetrics& /*fm*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr translateKnob = _translate.lock();

    if ( !translateKnob || !translateKnob->shouldDrawOverlayInteract() ) {
        return;
    }

    OfxPointD center;
    OfxPointD translate;
    OfxPointD scaleParam;
    bool scaleUniform;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    bool inverted = false;

    if (_mouseState == TransformInteract::eReleased) {
        getCenter(time, &center.x, &center.y);
        getTranslate(time, &translate.x, &translate.y);
        getScale(time, &scaleParam.x, &scaleParam.y);
        getScaleUniform(time, &scaleUniform);
        getRotate(time, &rotate);
        getSkewX(time, &skewX);
        getSkewY(time, &skewY);
        getSkewOrder(time, &skewOrder);
        inverted = getInvert(time);
    } else {
        center = _centerDrag;
        translate = _translateDrag;
        scaleParam = _scaleParamDrag;
        scaleUniform = _scaleUniformDrag;
        rotate = _rotateDrag;
        skewX = _skewXDrag;
        skewY = _skewYDrag;
        skewOrder = _skewOrderDrag;
        inverted = _invertedDrag;
    }

    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);


    GLdouble skewMatrix[16];
    skewMatrix[0] = ( skewOrder ? 1. : (1. + skewX * skewY) ); skewMatrix[1] = skewY; skewMatrix[2] = 0.; skewMatrix[3] = 0;
    skewMatrix[4] = skewX; skewMatrix[5] = (skewOrder ? (1. + skewX * skewY) : 1.); skewMatrix[6] = 0.; skewMatrix[7] = 0;
    skewMatrix[8] = 0.; skewMatrix[9] = 0.; skewMatrix[10] = 1.; skewMatrix[11] = 0;
    skewMatrix[12] = 0.; skewMatrix[13] = 0.; skewMatrix[14] = 0.; skewMatrix[15] = 1.;

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs

    GL_GPU::Disable(GL_LINE_STIPPLE);
    GL_GPU::Enable(GL_LINE_SMOOTH);
    GL_GPU::Disable(GL_POINT_SMOOTH);
    GL_GPU::Enable(GL_BLEND);
    GL_GPU::Hint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    GL_GPU::LineWidth(1.5f);
    GL_GPU::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        GL_GPU::MatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        GL_GPU::Translated(direction * shadow.x, -direction * shadow.y, 0);
        GL_GPU::MatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        GL_GPU::Color3f(color.r * l, color.g * l, color.b * l);

        GL_GPU::PushMatrix();
        GL_GPU::Translated(targetCenter.x, targetCenter.y, 0.);

        GL_GPU::Rotated(rotate, 0, 0., 1.);
        drawRotationBar(color, pscale, targetRadius.x, _mouseState == TransformInteract::eDraggingRotationBar || _drawState == TransformInteract::eRotationBarHovered, inverted, l);
        GL_GPU::MultMatrixd(skewMatrix);
        GL_GPU::Translated(-targetCenter.x, -targetCenter.y, 0.);

        drawEllipse(color, targetCenter, targetRadius, _mouseState == TransformInteract::eDraggingCircle || _drawState == TransformInteract::eCircleHovered, l);

        // add 180 to the angle to draw the arrows on the other side. unfortunately, this requires knowing
        // the mouse position in the ellipse frame
        double flip = 0.;
        if ( (_drawState == TransformInteract::eSkewXBarHoverered) || (_drawState == TransformInteract::eSkewYBarHoverered) ) {
            double rot = Transform::toRadians(rotate);
            Transform::Matrix3x3 transformscale;
            transformscale = Transform::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);

            Transform::Point3D previousPos;
            previousPos.x = lastPenPos.x();
            previousPos.y = lastPenPos.y();
            previousPos.z = 1.;
            previousPos = Transform::matApply(transformscale, previousPos);
            if (previousPos.z != 0) {
                previousPos.x /= previousPos.z;
                previousPos.y /= previousPos.z;
            }
            if ( ( (_drawState == TransformInteract::eSkewXBarHoverered) && (previousPos.y > targetCenter.y) ) ||
                 ( ( _drawState == TransformInteract::eSkewYBarHoverered) && ( previousPos.x > targetCenter.x) ) ) {
                flip = 180.;
            }
        }
        drawSkewBar(color, targetCenter, pscale, targetRadius.y, _mouseState == TransformInteract::eDraggingSkewXBar || _drawState == TransformInteract::eSkewXBarHoverered, flip, l);
        drawSkewBar(color, targetCenter, pscale, targetRadius.x, _mouseState == TransformInteract::eDraggingSkewYBar || _drawState == TransformInteract::eSkewYBarHoverered, flip - 90., l);


        drawSquare(color, targetCenter, pscale, _mouseState == TransformInteract::eDraggingTranslation || _mouseState == TransformInteract::eDraggingCenter || _drawState == TransformInteract::eCenterPointHovered, _modifierStateCtrl, l);
        drawSquare(color, left, pscale, _mouseState == TransformInteract::eDraggingLeftPoint || _drawState == TransformInteract::eLeftPointHovered, false, l);
        drawSquare(color, right, pscale, _mouseState == TransformInteract::eDraggingRightPoint || _drawState == TransformInteract::eRightPointHovered, false, l);
        drawSquare(color, top, pscale, _mouseState == TransformInteract::eDraggingTopPoint || _drawState == TransformInteract::eTopPointHovered, false, l);
        drawSquare(color, bottom, pscale, _mouseState == TransformInteract::eDraggingBottomPoint || _drawState == TransformInteract::eBottomPointHovered, false, l);

        GL_GPU::PopMatrix();
    }
    //glPopAttrib();
} // TransformInteract::draw

void
CornerPinInteract::draw(double time,
                        const RenderScale& /*renderScale*/,
                        ViewIdx /*view*/,
                        const OfxPointD& pscale,
                        const QPointF& /*lastPenPos*/,
                        const OfxRGBColourD& color,
                        const OfxPointD& shadow,
                        const QFont& font,
                        const QFontMetrics& /*fm*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr from1Knob = _from[0].lock();

    if ( !from1Knob || !from1Knob->shouldDrawOverlayInteract() ) {
        return;
    }

    GLdouble projection[16];
    GL_GPU::GetDoublev( GL_PROJECTION_MATRIX, projection);
    GLint viewport[4];
    GL_GPU::GetIntegerv(GL_VIEWPORT, viewport);


    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            getFrom(time, i, &from[i].x, &from[i].y);
            getTo(time, i, &to[i].x, &to[i].y);
            enable[i] = getEnabled(time, i);
        }
        useFrom = getUseFromPoints(time);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                q[i] = to[i];
            } else {
                q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs

    //glDisable(GL_LINE_STIPPLE);
    GL_GPU::Enable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    GL_GPU::Enable(GL_BLEND);
    GL_GPU::Hint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    GL_GPU::LineWidth(1.5f);
    GL_GPU::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GL_GPU::PointSize( CornerPinInteract::pointSize() );
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        GL_GPU::MatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        GL_GPU::Translated(direction * shadow.x, -direction * shadow.y, 0);
        GL_GPU::MatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        GL_GPU::Color3f( (float)(color.r / 2) * l, (float)(color.g / 2) * l, (float)(color.b / 2) * l );
        GL_GPU::Begin(GL_LINES);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                GL_GPU::Vertex2d(p[i].x, p[i].y);
                GL_GPU::Vertex2d(q[i].x, q[i].y);
            }
        }
        GL_GPU::End();
        GL_GPU::Color3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
        GL_GPU::Begin(GL_LINE_LOOP);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                GL_GPU::Vertex2d(p[i].x, p[i].y);
            }
        }
        GL_GPU::End();
        GL_GPU::Begin(GL_POINTS);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                if ( (_hovering == i) || (_dragging == i) ) {
                    GL_GPU::Color3f(0.f * l, 1.f * l, 0.f * l);
                } else {
                    GL_GPU::Color3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
                }
                GL_GPU::Vertex2d(p[i].x, p[i].y);
            }
        }
        GL_GPU::End();
        QColor c;
        c.setRgbF(color.r * l, color.g * l, color.b * l);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                renderText(p[i].x, p[i].y, pscale.x, pscale.y, useFrom ? QString::fromUtf8( _from[i].lock()->getName().c_str() ) : QString::fromUtf8( _to[i].lock()->getName().c_str() ), c, font);
            }
        }
    }

    //glPopAttrib();
} // CornerPinInteract::draw

void
HostOverlay::draw(double time,
                  const RenderScale& renderScale,
                  ViewIdx view)
{
    OfxRGBColourD color;

    if ( !getNode()->getNode()->getOverlayColor(&color.r, &color.g, &color.b) ) {
        color.r = color.g = color.b = 0.8;
    }
    OfxPointD pscale;
    n_getPixelScale(pscale.x, pscale.y);
    double w, h;
    n_getViewportSize(w, h);


    GLdouble projection[16];
    GL_GPU::GetDoublev( GL_PROJECTION_MATRIX, projection);
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    shadow.x = 2. / (projection[0] * w);
    shadow.y = 2. / (projection[5] * h);

    QFont font(appFont, appFontSize);
    QFontMetrics fm(font);
    // draw in reverse order
    for (InteractList::reverse_iterator it = _imp->interacts.rbegin(); it != _imp->interacts.rend(); ++it) {
        (*it)->draw(time, renderScale, view, pscale, _imp->lastPenPos, color, shadow, font, fm);
    }
}

bool
PositionInteract::penMotion(double time,
                            const RenderScale& /*renderScale*/,
                            ViewIdx view,
                            const OfxPointD& pscale,
                            const QPointF& lastPenPos,
                            const QPointF &penPos,
                            const QPoint & /*penPosViewport*/,
                            double /*pressure*/)
{
    KnobDoublePtr knob = _param.lock();

    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    if ( !knob || !knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    QPointF pos;
    if (_state == ePositionInteractStatePicked) {
        pos = lastPenPos;
    } else {
        double p[2];
        for (int i = 0; i < 2; ++i) {
            p[i] = knob->getValueAtTime(time, DimIdx(i));
            if (knob->getValueIsNormalized(DimIdx(i)) != eValueIsNormalizedNone) {
                p[i] = knob->denormalize(DimIdx(i), time, p[i]);
            }
        }
        pos.setX(p[0]);
        pos.setY(p[1]);
    }

    bool didSomething = false;
    bool valuesChanged = false;

    switch (_state) {
    case ePositionInteractStateInactive:
    case ePositionInteractStatePoised: {
        // are we in the box, become 'poised'
        PositionInteractState newState;
        if ( ( std::fabs( penPos.x() - pos.x() ) <= pointTolerance() * pscale.x) &&
             ( std::fabs( penPos.y() - pos.y() ) <= pointTolerance() * pscale.y) ) {
            newState = ePositionInteractStatePoised;
        } else {
            newState = ePositionInteractStateInactive;
        }

        if (_state != newState) {
            // state changed, must redraw
            requestRedraw();
        }
        _state = newState;
        //}
        break;
    }

    case ePositionInteractStatePicked: {
        valuesChanged = true;
        break;
    }
    }
    didSomething = (_state == ePositionInteractStatePoised) || (_state == ePositionInteractStatePicked);

    if ( (_state != ePositionInteractStateInactive) && _interactiveDrag && valuesChanged ) {
        std::vector<double> p(2);
        p[0] = fround(lastPenPos.x(), pscale.x);
        p[1] = fround(lastPenPos.y(), pscale.y);
        for (int i = 0; i < 2; ++i) {
            if (knob->getValueIsNormalized(DimIdx(i)) != eValueIsNormalizedNone) {
                p[i] = knob->normalize(DimIdx(i), time, p[i]);
            }
        }


        knob->setValueAcrossDimensions(p, DimIdx(0), ViewSetSpec(view), eValueChangedReasonUserEdited);
    }

    return (didSomething || valuesChanged);
} // PositionInteract::penMotion

static bool
squareContains(const Transform::Point3D& pos,
               const OfxRectD& rect,
               double toleranceX = 0.,
               double toleranceY = 0.)
{
    return ( pos.x >= (rect.x1 - toleranceX) && pos.x < (rect.x2 + toleranceX)
             && pos.y >= (rect.y1 - toleranceY) && pos.y < (rect.y2 + toleranceY) );
}

static bool
isOnEllipseBorder(const Transform::Point3D& pos,
                  const OfxPointD& targetRadius,
                  const OfxPointD& targetCenter,
                  double epsilon = 0.1)
{
    double v = ( (pos.x - targetCenter.x) * (pos.x - targetCenter.x) / (targetRadius.x * targetRadius.x) +
                 (pos.y - targetCenter.y) * (pos.y - targetCenter.y) / (targetRadius.y * targetRadius.y) );

    if ( ( v <= (1. + epsilon) ) && ( v >= (1. - epsilon) ) ) {
        return true;
    }

    return false;
}

static bool
isOnSkewXBar(const Transform::Point3D& pos,
             double targetRadiusY,
             const OfxPointD& center,
             const OfxPointD& pixelScale,
             double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusY + (20. * meanPixelScale);

    if ( ( pos.x >= (center.x - tolerance) ) && ( pos.x <= (center.x + tolerance) ) &&
         ( pos.y >= (center.y - barHalfSize - tolerance) ) && ( pos.y <= (center.y + barHalfSize + tolerance) ) ) {
        return true;
    }

    return false;
}

static bool
isOnSkewYBar(const Transform::Point3D& pos,
             double targetRadiusX,
             const OfxPointD& center,
             const OfxPointD& pixelScale,
             double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusX + (20. * meanPixelScale);

    if ( ( pos.y >= (center.y - tolerance) ) && ( pos.y <= (center.y + tolerance) ) &&
         ( pos.x >= (center.x - barHalfSize - tolerance) ) && ( pos.x <= (center.x + barHalfSize + tolerance) ) ) {
        return true;
    }

    return false;
}

static bool
isOnRotationBar(const Transform::Point3D& pos,
                double targetRadiusX,
                const OfxPointD& center,
                const OfxPointD& pixelScale,
                double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barExtra = 30. * meanPixelScale;

    if ( ( pos.x >= (center.x - tolerance) ) && ( pos.x <= (center.x + targetRadiusX + barExtra + tolerance) ) &&
         ( pos.y >= (center.y  - tolerance) ) && ( pos.y <= (center.y + tolerance) ) ) {
        return true;
    }

    return false;
}

static OfxRectD
rectFromCenterPoint(const OfxPointD& center,
                    const OfxPointD& pixelScale)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    OfxRectD ret;

    ret.x1 = center.x - (TransformInteract::pointSize() / 2.) * meanPixelScale;
    ret.x2 = center.x + (TransformInteract::pointSize() / 2.) * meanPixelScale;
    ret.y1 = center.y - (TransformInteract::pointSize() / 2.) * meanPixelScale;
    ret.y2 = center.y + (TransformInteract::pointSize() / 2.) * meanPixelScale;

    return ret;
}

bool
TransformInteract::penMotion(double time,
                             const RenderScale& /*renderScale*/,
                             ViewIdx view,
                             const OfxPointD& pscale,
                             const QPointF& lastPenPos,
                             const QPointF &penPosParam,
                             const QPoint & /*penPosViewport*/,
                             double /*pressure*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr translateKnob = _translate.lock();

    if ( !translateKnob || !translateKnob->shouldDrawOverlayInteract() ) {
        return false;
    }

    OfxPointD center;
    OfxPointD translate;
    OfxPointD scaleParam;
    bool scaleUniform;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    bool inverted = false;

    if (_mouseState == TransformInteract::eReleased) {
        getCenter(time, &center.x, &center.y);
        getTranslate(time, &translate.x, &translate.y);
        getScale(time, &scaleParam.x, &scaleParam.y);
        getScaleUniform(time, &scaleUniform);
        getRotate(time, &rotate);
        getSkewX(time, &skewX);
        getSkewY(time, &skewY);
        getSkewOrder(time, &skewOrder);
        inverted = getInvert(time);
    } else {
        center = _centerDrag;
        translate = _translateDrag;
        scaleParam = _scaleParamDrag;
        scaleUniform = _scaleUniformDrag;
        rotate = _rotateDrag;
        skewX = _skewXDrag;
        skewY = _skewYDrag;
        skewOrder = _skewOrderDrag;
        inverted = _invertedDrag;
    }

    bool didSomething = false;
    bool centerChanged = false;
    bool translateChanged = false;
    bool scaleChanged = false;
    bool rotateChanged = false;
    bool skewXChanged = false;
    bool skewYChanged = false;
    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(targetCenter, pscale);
    OfxRectD leftPoint = rectFromCenterPoint(left, pscale);
    OfxRectD rightPoint = rectFromCenterPoint(right, pscale);
    OfxRectD topPoint = rectFromCenterPoint(top, pscale);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom, pscale);


    //double dx = args.penPosition.x - _lastMousePos.x;
    //double dy = args.penPosition.y - _lastMousePos.y;
    double rot = Transform::toRadians(rotate);
    Transform::Point3D penPos, prevPenPos, rotationPos, transformedPos, previousPos, currentPos;
    penPos.x = penPosParam.x();
    penPos.y = penPosParam.y();
    penPos.z = 1.;
    prevPenPos.x = lastPenPos.x();
    prevPenPos.y = lastPenPos.y();
    prevPenPos.z = 1.;

    Transform::Matrix3x3 rotation, transform, transformscale;
    ////for the rotation bar/translation/center dragging we dont use the same transform, we don't want to undo the rotation transform
    if ( (_mouseState != TransformInteract::eDraggingTranslation) && (_mouseState != TransformInteract::eDraggingCenter) ) {
        ///undo skew + rotation to the current position
        rotation = Transform::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, targetCenter.x, targetCenter.y);
        transform = Transform::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);
        transformscale = Transform::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);
    } else {
        rotation = Transform::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, 0., targetCenter.x, targetCenter.y);
        transform = Transform::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, 0., targetCenter.x, targetCenter.y);
        transformscale = Transform::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, 0., targetCenter.x, targetCenter.y);
    }

    rotationPos = Transform::matApply(rotation, penPos);
    if (rotationPos.z != 0) {
        rotationPos.x /= rotationPos.z;
        rotationPos.y /= rotationPos.z;
    }

    transformedPos = Transform::matApply(transform, penPos);
    if (transformedPos.z != 0) {
        transformedPos.x /= transformedPos.z;
        transformedPos.y /= transformedPos.z;
    }

    previousPos = Transform::matApply(transformscale, prevPenPos);
    if (previousPos.z != 0) {
        previousPos.x /= previousPos.z;
        previousPos.y /= previousPos.z;
    }

    currentPos = Transform::matApply(transformscale, penPos);
    if (currentPos.z != 0) {
        currentPos.x /= currentPos.z;
        currentPos.y /= currentPos.z;
    }

    double minX, minY, maxX, maxY;
    KnobDoublePtr scaleKnob = _scale.lock();
    assert(scaleKnob);

    minX = scaleKnob->getMinimum(DimIdx(0));
    minY = scaleKnob->getMinimum(DimIdx(1));
    maxX = scaleKnob->getMaximum(DimIdx(0));
    maxY = scaleKnob->getMaximum(DimIdx(1));

    if (_mouseState == TransformInteract::eReleased) {
        // we are not axis-aligned
        double meanPixelScale = (pscale.x + pscale.y) / 2.;
        double hoverTolerance = (TransformInteract::pointSize() / 2.) * meanPixelScale;
        if ( squareContains(transformedPos, centerPoint) ) {
            _drawState = TransformInteract::eCenterPointHovered;
            didSomething = true;
        } else if ( squareContains(transformedPos, leftPoint) ) {
            _drawState = TransformInteract::eLeftPointHovered;
            didSomething = true;
        } else if ( squareContains(transformedPos, rightPoint) ) {
            _drawState = TransformInteract::eRightPointHovered;
            didSomething = true;
        } else if ( squareContains(transformedPos, topPoint) ) {
            _drawState = TransformInteract::eTopPointHovered;
            didSomething = true;
        } else if ( squareContains(transformedPos, bottomPoint) ) {
            _drawState = TransformInteract::eBottomPointHovered;
            didSomething = true;
        } else if ( isOnEllipseBorder(transformedPos, targetRadius, targetCenter) ) {
            _drawState = TransformInteract::eCircleHovered;
            didSomething = true;
        } else if ( isOnRotationBar(rotationPos, targetRadius.x, targetCenter, pscale, hoverTolerance) ) {
            _drawState = TransformInteract::eRotationBarHovered;
            didSomething = true;
        } else if ( isOnSkewXBar(transformedPos, targetRadius.y, targetCenter, pscale, hoverTolerance) ) {
            _drawState = TransformInteract::eSkewXBarHoverered;
            didSomething = true;
        } else if ( isOnSkewYBar(transformedPos, targetRadius.x, targetCenter, pscale, hoverTolerance) ) {
            _drawState = TransformInteract::eSkewYBarHoverered;
            didSomething = true;
        } else {
            _drawState = TransformInteract::eInActive;
        }
    } else if (_mouseState == TransformInteract::eDraggingCircle) {
        // we need to compute the backtransformed points with the scale

        // the scale ratio is the ratio of distances to the center
        double prevDistSq = (targetCenter.x - previousPos.x) * (targetCenter.x - previousPos.x) + (targetCenter.y - previousPos.y) * (targetCenter.y - previousPos.y);
        if (prevDistSq != 0.) {
            const double distSq = (targetCenter.x - currentPos.x) * (targetCenter.x - currentPos.x) + (targetCenter.y - currentPos.y) * (targetCenter.y - currentPos.y);
            const double distRatio = std::sqrt(distSq / prevDistSq);
            scale.x *= distRatio;
            scale.y *= distRatio;
            //_scale->setValue(scale.x, scale.y);
            scaleChanged = true;
        }
    } else if ( (_mouseState == TransformInteract::eDraggingLeftPoint) || (_mouseState == TransformInteract::eDraggingRightPoint) ) {
        // avoid division by zero
        if (targetCenter.x != previousPos.x) {
            const double scaleRatio = (targetCenter.x - currentPos.x) / (targetCenter.x - previousPos.x);
            OfxPointD newScale;
            newScale.x = scale.x * scaleRatio;
            newScale.x = std::max( minX, std::min(newScale.x, maxX) );
            newScale.y = scaleUniform ? newScale.x : scale.y;
            scale = newScale;
            //_scale->setValue(scale.x, scale.y);
            scaleChanged = true;
        }
    } else if ( (_mouseState == TransformInteract::eDraggingTopPoint) || (_mouseState == TransformInteract::eDraggingBottomPoint) ) {
        // avoid division by zero
        if (targetCenter.y != previousPos.y) {
            const double scaleRatio = (targetCenter.y - currentPos.y) / (targetCenter.y - previousPos.y);
            OfxPointD newScale;
            newScale.y = scale.y * scaleRatio;
            newScale.y = std::max( minY, std::min(newScale.y, maxY) );
            newScale.x = scaleUniform ? newScale.y : scale.x;
            scale = newScale;
            //_scale->setValue(scale.x, scale.y);
            scaleChanged = true;
        }
    } else if (_mouseState == TransformInteract::eDraggingTranslation) {
        double dx = penPosParam.x() - lastPenPos.x();
        double dy = penPosParam.y() - lastPenPos.y();

        if ( (_orientation == TransformInteract::eOrientationNotSet) && (_modifierStateShift > 0) ) {
            _orientation = std::abs(dx) > std::abs(dy) ? TransformInteract::eOrientationHorizontal : TransformInteract::eOrientationVertical;
        }

        dx = _orientation == TransformInteract::eOrientationVertical ? 0 : dx;
        dy = _orientation == TransformInteract::eOrientationHorizontal ? 0 : dy;
        double newx = translate.x + dx;
        double newy = translate.y + dy;
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
        newx = fround(newx, pscale.x);
        newy = fround(newy, pscale.y);
        translate.x = newx;
        translate.y = newy;
        //_translate->setValue(translate.x, translate.y);
        translateChanged = true;
    } else if (_mouseState == TransformInteract::eDraggingCenter) {
        OfxPointD currentCenter = center;
        Transform::Matrix3x3 R = Transform::matMul( Transform::matMul( Transform::matScale(1. / scale.x, 1. / scale.y), Transform::matSkewXY(-skewX, -skewY, !skewOrder) ), Transform::matRotation(rot) );
        double dx = penPosParam.x() - lastPenPos.x();
        double dy = penPosParam.y() - lastPenPos.y();

        if ( (_orientation == TransformInteract::eOrientationNotSet) && (_modifierStateShift > 0) ) {
            _orientation = std::abs(dx) > std::abs(dy) ? TransformInteract::eOrientationHorizontal : TransformInteract::eOrientationVertical;
        }

        dx = _orientation == TransformInteract::eOrientationVertical ? 0 : dx;
        dy = _orientation == TransformInteract::eOrientationHorizontal ? 0 : dy;

        Transform::Point3D dRot;
        dRot.x = dx;
        dRot.y = dy;
        dRot.z = 1.;
        dRot = Transform::matApply(R, dRot);
        if (dRot.z != 0) {
            dRot.x /= dRot.z;
            dRot.y /= dRot.z;
        }
        double dxrot = dRot.x;
        double dyrot = dRot.y;
        double newx = currentCenter.x + dxrot;
        double newy = currentCenter.y + dyrot;
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
        newx = fround(newx, pscale.x);
        newy = fround(newy, pscale.y);
        center.x = newx;
        center.y = newy;
        //_effect->beginEditBlock("setCenter");
        //_center->setValue(center.x, center.y);
        centerChanged = true;
        // recompute dxrot,dyrot after rounding
        double det = Transform::matDeterminant(R);
        if (det != 0.) {
            Transform::Matrix3x3 Rinv = Transform::matInverse(R, det);

            dxrot = newx - currentCenter.x;
            dyrot = newy - currentCenter.y;
            dRot.x = dxrot;
            dRot.y = dyrot;
            dRot.z = 1;
            dRot = Transform::matApply(Rinv, dRot);
            if (dRot.z != 0) {
                dRot.x /= dRot.z;
                dRot.y /= dRot.z;
            }
            dx = dRot.x;
            dy = dRot.y;
            OfxPointD newTranslation;
            newTranslation.x = translate.x + dx - dxrot;
            newTranslation.y = translate.y + dy - dyrot;
            translate = newTranslation;
            //_translate->setValue(translate.x, translate.y);
            translateChanged = true;
        }
        //_effect->endEditBlock();
    } else if (_mouseState == TransformInteract::eDraggingRotationBar) {
        OfxPointD diffToCenter;
        ///the current mouse position (untransformed) is doing has a certain angle relative to the X axis
        ///which can be computed by : angle = arctan(opposite / adjacent)
        diffToCenter.y = rotationPos.y - targetCenter.y;
        diffToCenter.x = rotationPos.x - targetCenter.x;
        double angle = std::atan2(diffToCenter.y, diffToCenter.x);
        double angledegrees = rotate + Transform::toDegrees(angle);
        double closest90 = 90. * std::floor( (angledegrees + 45.) / 90. );
        if (std::fabs(angledegrees - closest90) < 5.) {
            // snap to closest multiple of 90.
            angledegrees = closest90;
        }
        rotate = angledegrees;
        //_rotate->setValue(rotate);
        rotateChanged = true;
    } else if (_mouseState == TransformInteract::eDraggingSkewXBar) {
        // avoid division by zero
        if ( (scale.y != 0.) && (targetCenter.y != previousPos.y) ) {
            const double addSkew = (scale.x / scale.y) * (currentPos.x - previousPos.x) / (currentPos.y - targetCenter.y);
            skewX = skewX + addSkew;
            //_skewX->setValue(skewX);
            skewXChanged = true;
        }
    } else if (_mouseState == TransformInteract::eDraggingSkewYBar) {
        // avoid division by zero
        if ( (scale.x != 0.) && (targetCenter.x != previousPos.x) ) {
            const double addSkew = (scale.y / scale.x) * (currentPos.y - previousPos.y) / (currentPos.x - targetCenter.x);
            skewY = skewY + addSkew;
            //_skewY->setValue(skewY + addSkew);
            skewYChanged = true;
        }
    } else {
        assert(false);
    }

    _centerDrag = center;
    _translateDrag = translate;
    _scaleParamDrag = scale;
    _scaleUniformDrag = scaleUniform;
    _rotateDrag = rotate;
    _skewXDrag = skewX;
    _skewYDrag = skewY;
    _skewOrderDrag = skewOrder;
    _invertedDrag = inverted;

    bool valuesChanged = (centerChanged || translateChanged || scaleChanged || rotateChanged || skewXChanged || skewYChanged);

    if ( (_mouseState != TransformInteract::eReleased) && _interactiveDrag && valuesChanged ) {
        // no need to redraw overlay since it is slave to the paramaters
        EffectInstancePtr holder = _overlay->getNode()->getNode()->getEffectInstance();
        holder->beginMultipleEdits(tr("Modify transform handle").toStdString());
        if (centerChanged) {
            KnobDoublePtr knob = _center.lock();
            std::vector<double> val(2);
            val[0] = center.x;
            val[1] = center.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        if (translateChanged) {
            KnobDoublePtr knob = _translate.lock();
            std::vector<double> val(2);
            val[0] = translate.x;
            val[1] = translate.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        if (scaleChanged) {
            KnobDoublePtr knob = _scale.lock();
            std::vector<double> val(2);
            val[0] = scale.x;
            val[1] = scale.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        if (rotateChanged) {
            KnobDoublePtr knob = _rotate.lock();
            knob->setValue(rotate, view, DimIdx(0), eValueChangedReasonUserEdited);
        }
        if (skewXChanged) {
            KnobDoublePtr knob = _skewX.lock();
            knob->setValue(skewX, view, DimIdx(0), eValueChangedReasonUserEdited);
        }
        if (skewYChanged) {
            KnobDoublePtr knob = _skewY.lock();
            knob->setValue(skewY, view, DimIdx(0), eValueChangedReasonUserEdited);
        }
        holder->endMultipleEdits();
    } else if (didSomething || valuesChanged) {
        requestRedraw();
    }

    return didSomething || valuesChanged;
} // TransformInteract::penMotion

bool
CornerPinInteract::penMotion(double time,
                             const RenderScale& /*renderScale*/,
                             ViewIdx view,
                             const OfxPointD& pscale,
                             const QPointF& lastPenPos,
                             const QPointF &penPos,
                             const QPoint & /*penPosViewport*/,
                             double /*pressure*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr from1Knob = _from[0].lock();

    if ( !from1Knob || !from1Knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            getFrom(time, i, &from[i].x, &from[i].y);
            getTo(time, i, &to[i].x, &to[i].y);
            enable[i] = getEnabled(time, i);
        }
        useFrom = getUseFromPoints(time);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }


    OfxPointD p[4];
    OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                q[i] = to[i];
            } else {
                q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    bool didSomething = false;
    bool valuesChanged = false;
    OfxPointD delta;
    delta.x = penPos.x() - lastPenPos.x();
    delta.y = penPos.y() - lastPenPos.y();

    _hovering = -1;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if (_dragging == i) {
                if (useFrom) {
                    from[i].x += delta.x;
                    from[i].y += delta.y;
                    _fromDrag[i] = from[i];
                } else {
                    to[i].x += delta.x;
                    to[i].y += delta.y;
                    _toDrag[i] = to[i];
                }
                valuesChanged = true;
            } else if ( CornerPinInteract::isNearby(penPos, p[i].x, p[i].y, CornerPinInteract::pointTolerance(), pscale) ) {
                _hovering = i;
                didSomething = true;
            }
        }
    }

    if ( (_dragging != -1) && _interactiveDrag && valuesChanged ) {
        // no need to redraw overlay since it is slave to the paramaters

        if (_useFromDrag) {
            KnobDoublePtr knob = _from[_dragging].lock();
            assert(knob);
            std::vector<double> val(2);
            val[0] = from[_dragging].x;
            val[1] = from[_dragging].y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        } else {
            KnobDoublePtr knob = _to[_dragging].lock();
            assert(knob);
            std::vector<double> val(2);
            val[0] = to[_dragging].x;
            val[1] = to[_dragging].y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
    }

    return didSomething || valuesChanged;
} // CornerPinInteract::penMotion

bool
HostOverlay::penMotion(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       const QPointF &penPos,
                       const QPoint &penPosViewport,
                       double pressure)
{
    OfxPointD pscale;

    n_getPixelScale(pscale.x, pscale.y);
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->penMotion(time, renderScale, view, pscale, _imp->lastPenPos, penPos, penPosViewport, pressure) ) {
            _imp->lastPenPos = penPos;

            return true;
        }
    }

    _imp->lastPenPos = penPos;

    return false;
}

bool
PositionInteract::penUp(double time,
                        const RenderScale& renderScale,
                        ViewIdx view,
                        const OfxPointD& pscale,
                        const QPointF& lastPenPos,
                        const QPointF &penPos,
                        const QPoint &penPosViewport,
                        double pressure)
{
    KnobDoublePtr knob = _param.lock();

    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    if ( !knob || !knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    bool didSomething = false;
    if (_state == ePositionInteractStatePicked) {
        if (!_interactiveDrag) {
            KnobDoublePtr knob = _param.lock();
             std::vector<double> p(2);
            p[0] = fround(lastPenPos.x(), pscale.x);
            p[1] = fround(lastPenPos.y(), pscale.y);
            for (int i = 0; i < 2; ++i) {
                if (knob->getValueIsNormalized(DimIdx(i)) != eValueIsNormalizedNone) {
                    p[i] = knob->normalize(DimIdx(i), time, p[i]);
                }
            }

            knob->setValueAcrossDimensions(p, DimIdx(0), view, eValueChangedReasonUserEdited);
        }

        _state = ePositionInteractStateInactive;
        bool motion = penMotion(time, renderScale, view, pscale, lastPenPos, penPos, penPosViewport, pressure);
        Q_UNUSED(motion);
        didSomething = true;
    }

    return didSomething;
}

bool
TransformInteract::penUp(double /*time*/,
                         const RenderScale& /*renderScale*/,
                         ViewIdx view,
                         const OfxPointD& /*pscale*/,
                         const QPointF& /*lastPenPos*/,
                         const QPointF & /*penPos*/,
                         const QPoint & /*penPosViewport*/,
                         double /*pressure*/)
{
    bool ret = _mouseState != TransformInteract::eReleased;

    if ( !_interactiveDrag && (_mouseState != TransformInteract::eReleased) ) {
        // no need to redraw overlay since it is slave to the paramaters

        /*
           Give eValueChangedReasonPluginEdited reason so that the command uses the undo/redo stack
           see Knob::setValue
         */
        EffectInstancePtr holder = _overlay->getNode()->getNode()->getEffectInstance();
        holder->beginMultipleEdits(tr("Modify transform handle").toStdString());
        {
            KnobDoublePtr knob = _center.lock();
            std::vector<double> val(2);
            val[0] = _centerDrag.x;
            val[1] = _centerDrag.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        {
            KnobDoublePtr knob = _translate.lock();
            std::vector<double> val(2);
            val[0] = _translateDrag.x;
            val[1] = _translateDrag.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        {
            KnobDoublePtr knob = _scale.lock();
            std::vector<double> val(2);
            val[0] = _scaleParamDrag.x;
            val[1] = _scaleParamDrag.y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
        {
            KnobDoublePtr knob = _rotate.lock();
            knob->setValue(_rotateDrag, view, DimIdx(0), eValueChangedReasonUserEdited);
        }
        {
            KnobDoublePtr knob = _skewX.lock();
            knob->setValue(_skewXDrag, view, DimIdx(0), eValueChangedReasonUserEdited);
        }
        {
            KnobDoublePtr knob = _skewY.lock();
            knob->setValue(_skewYDrag, view, DimIdx(0), eValueChangedReasonUserEdited);
        }

        holder->endMultipleEdits();
    } else if (_mouseState != TransformInteract::eReleased) {
        requestRedraw();
    }

    _mouseState = TransformInteract::eReleased;

    return ret;
}

bool
CornerPinInteract::penUp(double /*time*/,
                         const RenderScale& /*renderScale*/,
                         ViewIdx view,
                         const OfxPointD& /*pscale*/,
                         const QPointF& /*lastPenPos*/,
                         const QPointF & /*penPos*/,
                         const QPoint & /*penPosViewport*/,
                         double /*pressure*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr from1Knob = _from[0].lock();

    if ( !from1Knob || !from1Knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    bool didSomething = _dragging != -1;

    if ( !_interactiveDrag && (_dragging != -1) ) {
        // no need to redraw overlay since it is slave to the paramaters
        if (_useFromDrag) {
            KnobDoublePtr knob = _from[_dragging].lock();
            assert(knob);
            std::vector<double> val(2);
            val[0] = _fromDrag[_dragging].x;
            val[1] = _fromDrag[_dragging].y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        } else {
            KnobDoublePtr knob = _to[_dragging].lock();
            assert(knob);
            std::vector<double> val(2);
            val[0] = _toDrag[_dragging].x;
            val[1] = _toDrag[_dragging].y;
            knob->setValueAcrossDimensions(val, DimIdx(0), view, eValueChangedReasonUserEdited);
        }
    }
    _dragging = -1;

    return didSomething;
}

bool
HostOverlay::penUp(double time,
                   const RenderScale& renderScale,
                   ViewIdx view,
                   const QPointF &penPos,
                   const QPoint &penPosViewport,
                   double pressure)
{
    OfxPointD pscale;

    n_getPixelScale(pscale.x, pscale.y);

    bool didSomething = false;
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        didSomething |= (*it)->penUp(time, renderScale, view, pscale, _imp->lastPenPos, penPos, penPosViewport, pressure);
    }

    return didSomething;
}

bool
PositionInteract::penDown(double time,
                          const RenderScale& renderScale,
                          ViewIdx view,
                          const OfxPointD& pscale,
                          const QPointF& lastPenPos,
                          const QPointF &penPos,
                          const QPoint &penPosViewport,
                          double pressure)
{
    KnobDoublePtr knob = _param.lock();

    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    if ( !knob || !knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    bool motion = penMotion(time, renderScale, view, pscale, lastPenPos, penPos, penPosViewport, pressure);
    Q_UNUSED(motion);
    if (_state == ePositionInteractStatePoised) {
        _state = ePositionInteractStatePicked;
        _interactiveDrag = getInteractive(time);

        return true;
    }

    return false;
}

bool
TransformInteract::penDown(double time,
                           const RenderScale& /*renderScale*/,
                           ViewIdx /*view*/,
                           const OfxPointD& pscale,
                           const QPointF& /*lastPenPos*/,
                           const QPointF &penPosParam,
                           const QPoint & /*penPosViewport*/,
                           double /*pressure*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr translateKnob = _translate.lock();

    if ( !translateKnob || !translateKnob->shouldDrawOverlayInteract() ) {
        return false;
    }

    OfxPointD center;
    OfxPointD translate;
    OfxPointD scaleParam;
    bool scaleUniform;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    bool inverted = false;

    if (_mouseState == TransformInteract::eReleased) {
        getCenter(time, &center.x, &center.y);
        getTranslate(time, &translate.x, &translate.y);
        getScale(time, &scaleParam.x, &scaleParam.y);
        getScaleUniform(time, &scaleUniform);
        getRotate(time, &rotate);
        getSkewX(time, &skewX);
        getSkewY(time, &skewY);
        getSkewOrder(time, &skewOrder);
        inverted = getInvert(time);
        _interactiveDrag = getInteractive(time);
    } else {
        center = _centerDrag;
        translate = _translateDrag;
        scaleParam = _scaleParamDrag;
        scaleUniform = _scaleUniformDrag;
        rotate = _rotateDrag;
        skewX = _skewXDrag;
        skewY = _skewYDrag;
        skewOrder = _skewOrderDrag;
        inverted = _invertedDrag;
    }


    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(targetCenter, pscale);
    OfxRectD leftPoint = rectFromCenterPoint(left, pscale);
    OfxRectD rightPoint = rectFromCenterPoint(right, pscale);
    OfxRectD topPoint = rectFromCenterPoint(top, pscale);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom, pscale);
    Transform::Point3D transformedPos, rotationPos;
    transformedPos.x = penPosParam.x();
    transformedPos.y = penPosParam.y();
    transformedPos.z = 1.;

    double rot = Transform::toRadians(rotate);

    ///now undo skew + rotation to the current position
    Transform::Matrix3x3 rotation, transform;
    rotation = Transform::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, targetCenter.x, targetCenter.y);
    transform = Transform::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);

    rotationPos = Transform::matApply(rotation, transformedPos);
    if (rotationPos.z != 0) {
        rotationPos.x /= rotationPos.z;
        rotationPos.y /= rotationPos.z;
    }
    transformedPos = Transform::matApply(transform, transformedPos);
    if (transformedPos.z != 0) {
        transformedPos.x /= transformedPos.z;
        transformedPos.y /= transformedPos.z;
    }

    _orientation = TransformInteract::eOrientationAllDirections;

    double pressToleranceX = 5 * pscale.x;
    double pressToleranceY = 5 * pscale.y;
    bool didSomething = false;
    if ( squareContains(transformedPos, centerPoint, pressToleranceX, pressToleranceY) ) {
        _mouseState = _modifierStateCtrl ? TransformInteract::eDraggingCenter : TransformInteract::eDraggingTranslation;
        if (_modifierStateShift > 0) {
            _orientation = TransformInteract::eOrientationNotSet;
        }
        didSomething = true;
    } else if ( squareContains(transformedPos, leftPoint, pressToleranceX, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingLeftPoint;
        didSomething = true;
    } else if ( squareContains(transformedPos, rightPoint, pressToleranceX, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingRightPoint;
        didSomething = true;
    } else if ( squareContains(transformedPos, topPoint, pressToleranceX, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingTopPoint;
        didSomething = true;
    } else if ( squareContains(transformedPos, bottomPoint, pressToleranceX, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingBottomPoint;
        didSomething = true;
    } else if ( isOnEllipseBorder(transformedPos, targetRadius, targetCenter) ) {
        _mouseState = TransformInteract::eDraggingCircle;
        didSomething = true;
    } else if ( isOnRotationBar(rotationPos, targetRadius.x, targetCenter, pscale, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingRotationBar;
        didSomething = true;
    } else if ( isOnSkewXBar(transformedPos, targetRadius.y, targetCenter, pscale, pressToleranceY) ) {
        _mouseState = TransformInteract::eDraggingSkewXBar;
        didSomething = true;
    } else if ( isOnSkewYBar(transformedPos, targetRadius.x, targetCenter, pscale, pressToleranceX) ) {
        _mouseState = TransformInteract::eDraggingSkewYBar;
        didSomething = true;
    } else {
        _mouseState = TransformInteract::eReleased;
    }

    _centerDrag = center;
    _translateDrag = translate;
    _scaleParamDrag = scaleParam;
    _scaleUniformDrag = scaleUniform;
    _rotateDrag = rotate;
    _skewXDrag = skewX;
    _skewYDrag = skewY;
    _skewOrderDrag = skewOrder;
    _invertedDrag = inverted;

    if (didSomething) {
        requestRedraw();
    }

    return didSomething;
} // TransformInteract::penDown

bool
CornerPinInteract::penDown(double time,
                           const RenderScale& /*renderScale*/,
                           ViewIdx /*view*/,
                           const OfxPointD& pscale,
                           const QPointF& /*lastPenPos*/,
                           const QPointF &penPos,
                           const QPoint & /*penPosViewport*/,
                           double /*pressure*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr from1Knob = _from[0].lock();

    if ( !from1Knob || !from1Knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            getFrom(time, i, &from[i].x, &from[i].y);
            getTo(time, i, &to[i].x, &to[i].y);
            enable[i] = getEnabled(time, i);
        }
        useFrom = getUseFromPoints(time);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        _interactiveDrag = getInteractive(time);
        useFrom = _useFromDrag;
    }


    OfxPointD p[4];
    OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                q[i] = to[i];
            } else {
                q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }
    bool didSomething = false;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if ( CornerPinInteract::isNearby(penPos, p[i].x, p[i].y, CornerPinInteract::pointTolerance(), pscale) ) {
                _dragging = i;
                didSomething = true;
            }
            _toDrag[i] = to[i];
            _fromDrag[i] = from[i];
            _enableDrag[i] = enable[i];
        }
    }
    _useFromDrag = useFrom;


    return didSomething;
} // CornerPinInteract::penDown

bool
HostOverlay::penDown(double time,
                     const RenderScale& renderScale,
                     ViewIdx view,
                     const QPointF &penPos,
                     const QPoint &penPosViewport,
                     double pressure)
{
    OfxPointD pscale;

    n_getPixelScale(pscale.x, pscale.y);
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->penDown(time, renderScale, view, pscale, _imp->lastPenPos, penPos, penPosViewport, pressure) ) {
            _imp->lastPenPos = penPos;

            return true;
        }
    }

    _imp->lastPenPos = penPos;

    return false;
}

bool
HostOverlay::penDoubleClicked(double time,
                              const RenderScale& renderScale,
                              ViewIdx view,
                              const QPointF &penPos,
                              const QPoint &penPosViewport)
{
    OfxPointD pscale;

    n_getPixelScale(pscale.x, pscale.y);
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->penDoubleClicked(time, renderScale, view, pscale, _imp->lastPenPos, penPos, penPosViewport) ) {
            _imp->lastPenPos = penPos;

            return true;
        }
    }

    _imp->lastPenPos = penPos;

    return false;
}

bool
TransformInteract::keyDown(double /*time*/,
                           const RenderScale& /*renderScale*/,
                           ViewIdx /*view*/,
                           int key,
                           char*   /*keyString*/)
{
    // Always process, even if interact is not open, since this concerns modifiers
    //if (!_interactOpen->getValueAtTime(args.time)) {
    //    return false;
    //}

    // Note that on the Mac:
    // cmd/apple/cloverleaf is kOfxKey_Control_L
    // ctrl is kOfxKey_Meta_L
    // alt/option is kOfxKey_Alt_L
    bool mustRedraw = false;

    // the two control keys may be pressed consecutively, be aware about this
    if ( (key == kOfxKey_Control_L) || (key == kOfxKey_Control_R) ) {
        mustRedraw = _modifierStateCtrl == 0;
        ++_modifierStateCtrl;
    }
    if ( (key == kOfxKey_Shift_L) || (key == kOfxKey_Shift_R) ) {
        mustRedraw = _modifierStateShift == 0;
        ++_modifierStateShift;
        if (_modifierStateShift > 0) {
            _orientation = TransformInteract::eOrientationNotSet;
        }
    }
    if (mustRedraw) {
        requestRedraw();
    }
    //std::cout << std::hex << args.keySymbol << std::endl;

    // modifiers are not "caught"
    return false;
}

bool
HostOverlay::keyDown(double time,
                     const RenderScale& renderScale,
                     ViewIdx view,
                     int key,
                     char*   keyString)
{
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->keyDown(time, renderScale, view, key, keyString) ) {
            return true;
        }
    }

    return false;
}

bool
TransformInteract::keyUp(double /*time*/,
                         const RenderScale& /*renderScale*/,
                         ViewIdx /*view*/,
                         int key,
                         char*   /*keyString*/)
{
    bool mustRedraw = false;

    if ( (key == kOfxKey_Control_L) || (key == kOfxKey_Control_R) ) {
        // we may have missed a keypress
        if (_modifierStateCtrl > 0) {
            --_modifierStateCtrl;
            mustRedraw = _modifierStateCtrl == 0;
        }
    }
    if ( (key == kOfxKey_Shift_L) || (key == kOfxKey_Shift_R) ) {
        if (_modifierStateShift > 0) {
            --_modifierStateShift;
            mustRedraw = _modifierStateShift == 0;
        }
        if (_modifierStateShift == 0) {
            _orientation = TransformInteract::eOrientationAllDirections;
        }
    }
    if (mustRedraw) {
        requestRedraw();
    }

    // modifiers are not "caught"
    return false;
}

bool
HostOverlay::keyUp(double time,
                   const RenderScale& renderScale,
                   ViewIdx view,
                   int key,
                   char*   keyString)
{
    bool didSomething = false;

    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        didSomething |= (*it)->keyUp(time, renderScale, view, key, keyString);
    }

    return didSomething;
}

bool
HostOverlay::keyRepeat(double time,
                       const RenderScale& renderScale,
                       ViewIdx view,
                       int key,
                       char*   keyString)
{
    bool didSomething = false;

    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        didSomething |= (*it)->keyRepeat(time, renderScale, view, key, keyString);
    }

    return didSomething;
}

bool
HostOverlay::gainFocus(double time,
                       const RenderScale& renderScale,
                       ViewIdx view)
{
    bool didSomething = false;

    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        didSomething |= (*it)->gainFocus(time, renderScale, view);
    }

    return didSomething;
}

bool
PositionInteract::loseFocus(double /*time*/,
                            const RenderScale & /*renderScale*/,
                            ViewIdx /*view*/)
{
    KnobDoublePtr knob = _param.lock();

    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    if ( !knob || !knob->shouldDrawOverlayInteract() ) {
        return false;
    }

    if (_state != ePositionInteractStateInactive) {
        _state = ePositionInteractStateInactive;
        // state changed, must redraw
        requestRedraw();
    }

    return false;
}

bool
TransformInteract::loseFocus(double /*time*/,
                             const RenderScale & /*renderScale*/,
                             ViewIdx /*view*/)
{
    // do not show interact if knob is secret or not enabled
    // see https://github.com/MrKepzie/Natron/issues/932
    KnobDoublePtr translateKnob = _translate.lock();

    if ( !translateKnob || !translateKnob->shouldDrawOverlayInteract() ) {
        return false;
    }
    // reset the modifiers state
    _modifierStateCtrl = 0;
    _modifierStateShift = 0;
    _interactiveDrag = false;
    _mouseState = TransformInteract::eReleased;
    _drawState = TransformInteract::eInActive;

    return false;
}

bool
CornerPinInteract::loseFocus(double /*time*/,
                             const RenderScale & /*renderScale*/,
                             ViewIdx /*view*/)
{
    _dragging = -1;
    _hovering = -1;
    _interactiveDrag = false;

    return false;
}

bool
HostOverlay::loseFocus(double time,
                       const RenderScale &renderScale,
                       ViewIdx view)
{
    bool didSomething = false;

    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        didSomething |= (*it)->loseFocus(time, renderScale, view);
    }

    return didSomething;
}

bool
HostOverlay::hasHostOverlayForParam(const KnobIConstPtr& param)
{
    assert( QThread::currentThread() == qApp->thread() );
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->isInteractForKnob(param) ) {
            return true;
        }
    }

    return false;
}

void
HostOverlay::removePositionHostOverlay(const KnobIPtr& knob)
{
    for (InteractList::iterator it = _imp->interacts.begin(); it != _imp->interacts.end(); ++it) {
        if ( (*it)->isInteractForKnob(knob) ) {
            _imp->interacts.erase(it);

            return;
        }
    }
}

bool
HostOverlay::isEmpty() const
{
    if ( _imp->interacts.empty() ) {
        return true;
    }

    return false;
}

NATRON_NAMESPACE_EXIT;
