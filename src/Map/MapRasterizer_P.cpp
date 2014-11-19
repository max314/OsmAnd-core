#include "MapRasterizer_P.h"
#include "MapRasterizer.h"
#include "MapRasterizer_Metrics.h"

#include "QtCommon.h"
#include <QReadWriteLock>

#include "ignore_warnings_on_external_includes.h"
#include <SkBitmapDevice.h>
#include <SkBlurMaskFilter.h>
#include <SkBlurDrawLooper.h>
#include <SkColorFilter.h>
#include <SkDashPathEffect.h>
#include <SkBitmapProcShader.h>
#include <SkError.h>
#include "restore_internal_warnings.h"

#include "MapPresentationEnvironment.h"
#include "MapStyleEvaluationResult.h"
#include "MapStyleBuiltinValueDefinitions.h"
#include "MapPrimitiviser.h"
#include "BinaryMapObject.h"
#include "ObfMapSectionInfo.h"
#include "QKeyValueIterator.h"
#include "QCachingIterator.h"
#include "Stopwatch.h"
#include "Utilities.h"
#include "Logging.h"

OsmAnd::MapRasterizer_P::MapRasterizer_P(MapRasterizer* const owner_)
    : owner(owner_)
{
}

OsmAnd::MapRasterizer_P::~MapRasterizer_P()
{
    {
        QMutexLocker scopedLocker(&_pathEffectsMutex);

        for (auto& pathEffect : _pathEffects)
            pathEffect->unref();
    }
}

void OsmAnd::MapRasterizer_P::initialize()
{
    _defaultPaint.setAntiAlias(true);

    const auto intervalW = 0.5f*owner->mapPresentationEnvironment->displayDensityFactor + 0.5f; // 0.5dp + 0.5px
    {
        const float intervals_oneway[4][4] =
        {
            { 0, 12, 10 * intervalW, 152 },
            { 0, 12, 9 * intervalW, 152 + intervalW },
            { 0, 12 + 6 * intervalW, 2 * intervalW, 152 + 2 * intervalW },
            { 0, 12 + 6 * intervalW, 1 * intervalW, 152 + 3 * intervalW }
        };
        SkPathEffect* arrowDashEffect1 = SkDashPathEffect::Create(intervals_oneway[0], 4, 0);
        SkPathEffect* arrowDashEffect2 = SkDashPathEffect::Create(intervals_oneway[1], 4, 1);
        SkPathEffect* arrowDashEffect3 = SkDashPathEffect::Create(intervals_oneway[2], 4, 1);
        SkPathEffect* arrowDashEffect4 = SkDashPathEffect::Create(intervals_oneway[3], 4, 1);

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(1.0f * intervalW);
            paint.setPathEffect(arrowDashEffect1)->unref();
            _oneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(2.0f * intervalW);
            paint.setPathEffect(arrowDashEffect2)->unref();
            _oneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(3.0f * intervalW);
            paint.setPathEffect(arrowDashEffect3)->unref();
            _oneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(4.0f * intervalW);
            paint.setPathEffect(arrowDashEffect4)->unref();
            _oneWayPaints.push_back(qMove(paint));
        }
    }

    {
        const float intervals_reverse[4][4] =
        {
            { 0, 12, 10 * intervalW, 152 },
            { 0, 12 + 1 * intervalW, 9 * intervalW, 152 },
            { 0, 12 + 2 * intervalW, 2 * intervalW, 152 + 6 * intervalW },
            { 0, 12 + 3 * intervalW, 1 * intervalW, 152 + 6 * intervalW }
        };
        SkPathEffect* arrowDashEffect1 = SkDashPathEffect::Create(intervals_reverse[0], 4, 0);
        SkPathEffect* arrowDashEffect2 = SkDashPathEffect::Create(intervals_reverse[1], 4, 1);
        SkPathEffect* arrowDashEffect3 = SkDashPathEffect::Create(intervals_reverse[2], 4, 1);
        SkPathEffect* arrowDashEffect4 = SkDashPathEffect::Create(intervals_reverse[3], 4, 1);

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(1.0f * intervalW);
            paint.setPathEffect(arrowDashEffect1)->unref();
            _reverseOneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(2.0f * intervalW);
            paint.setPathEffect(arrowDashEffect2)->unref();
            _reverseOneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(3.0f * intervalW);
            paint.setPathEffect(arrowDashEffect3)->unref();
            _reverseOneWayPaints.push_back(qMove(paint));
        }

        {
            SkPaint paint;
            initializeOneWayPaint(paint);
            paint.setStrokeWidth(4.0f * intervalW);
            paint.setPathEffect(arrowDashEffect4)->unref();
            _reverseOneWayPaints.push_back(qMove(paint));
        }
    }
}

void OsmAnd::MapRasterizer_P::initializeOneWayPaint(SkPaint& paint)
{
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setColor(0xff6c70d5);
}

void OsmAnd::MapRasterizer_P::rasterize(
    const AreaI area31,
    const std::shared_ptr<const MapPrimitiviser::PrimitivisedObjects>& primitivisedObjects,
    SkCanvas& canvas,
    const bool fillBackground,
    const AreaI* const pDestinationArea,
    MapRasterizer_Metrics::Metric_rasterize* const metric,
    const IQueryController* const controller)
{
    const Stopwatch totalStopwatch(metric != nullptr);

    const Context context(area31, primitivisedObjects);

    // Deal with background
    if (fillBackground)
    {
        // Get default background color
        const auto defaultBackgroundColor = context.env->getDefaultBackgroundColor(context.zoom);

        if (pDestinationArea)
        {
            // If destination area is specified, fill only it with background
            SkPaint bgPaint;
            bgPaint.setColor(defaultBackgroundColor.toSkColor());
            bgPaint.setStyle(SkPaint::kFill_Style);
            canvas.drawRectCoords(
                pDestinationArea->top(),
                pDestinationArea->left(),
                pDestinationArea->right(),
                pDestinationArea->bottom(),
                bgPaint);
        }
        else
        {
            // Since destination area is not specified, erase whole canvas with specified color
            canvas.clear(defaultBackgroundColor.toSkColor());
        }
    }

    // Precalculate values
    AreaI destinationArea;
    if (pDestinationArea)
    {
        destinationArea = *pDestinationArea;
    }
    else
    {
        const auto targetSize = canvas.getDeviceSize();
        destinationArea = AreaI(0, 0, targetSize.height(), targetSize.width());
    }

    // Rasterize layers of map:
    rasterizeMapPrimitives(context, canvas, primitivisedObjects->polygons, PrimitivesType::Polygons, controller);
    if (context.shadowMode != MapPresentationEnvironment::ShadowMode::NoShadow)
        rasterizeMapPrimitives(context, canvas, primitivisedObjects->polylines, PrimitivesType::Polylines_ShadowOnly, controller);
    rasterizeMapPrimitives(context, canvas, primitivisedObjects->polylines, PrimitivesType::Polylines, controller);

    if (metric)
        metric->elapsedTime += totalStopwatch.elapsed();
}

void OsmAnd::MapRasterizer_P::rasterizeMapPrimitives(
    const Context& context,
    SkCanvas& canvas,
    const MapPrimitiviser::PrimitivesCollection& primitives,
    PrimitivesType type,
    const IQueryController* const controller)
{
    assert(type != PrimitivesType::Points);

    for (const auto& primitive : constOf(primitives))
    {
        if (controller && controller->isAborted())
            return;

        if (type == PrimitivesType::Polygons)
        {
            rasterizePolygon(
                context,
                canvas,
                primitive);
        }
        else if (type == PrimitivesType::Polylines || type == PrimitivesType::Polylines_ShadowOnly)
        {
            rasterizePolyline(
                context,
                canvas,
                primitive,
                (type == PrimitivesType::Polylines_ShadowOnly));
        }
    }
}

bool OsmAnd::MapRasterizer_P::updatePaint(
    const Context& context,
    SkPaint& paint,
    const MapStyleEvaluationResult& evalResult,
    const PaintValuesSet valueSetSelector,
    const bool isArea)
{
    const auto& env = context.env;

    bool ok = true;

    int valueDefId_color = -1;
    int valueDefId_strokeWidth = -1;
    int valueDefId_cap = -1;
    int valueDefId_pathEffect = -1;
    switch (valueSetSelector)
    {
        case PaintValuesSet::Set_0:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT;
            break;
        case PaintValuesSet::Set_1:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR_2;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH_2;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP_2;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT_2;
            break;
        case PaintValuesSet::Set_minus1:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR_0;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH_0;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP_0;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT_0;
            break;
        case PaintValuesSet::Set_minus2:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR__1;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH__1;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP__1;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT__1;
            break;
        case PaintValuesSet::Set_3:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR_3;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH_3;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP_3;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT_3;
            break;
        case PaintValuesSet::Set_4:
            valueDefId_color = env->styleBuiltinValueDefs->id_OUTPUT_COLOR_4;
            valueDefId_strokeWidth = env->styleBuiltinValueDefs->id_OUTPUT_STROKE_WIDTH_4;
            valueDefId_cap = env->styleBuiltinValueDefs->id_OUTPUT_CAP_4;
            valueDefId_pathEffect = env->styleBuiltinValueDefs->id_OUTPUT_PATH_EFFECT_4;
            break;
    }

    if (isArea)
    {
        if (!evalResult.contains(valueDefId_color) && !evalResult.contains(env->styleBuiltinValueDefs->id_OUTPUT_SHADER))
            return false;

        paint.setColorFilter(nullptr);
        paint.setShader(nullptr);
        paint.setLooper(nullptr);
        paint.setStyle(SkPaint::kStrokeAndFill_Style);
        paint.setStrokeWidth(0);
    }
    else
    {
        float stroke;
        ok = evalResult.getFloatValue(valueDefId_strokeWidth, stroke);
        if (!ok || stroke <= 0.0f)
            return false;

        paint.setColorFilter(nullptr);
        paint.setShader(nullptr);
        paint.setLooper(nullptr);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(stroke);

        QString cap;
        ok = evalResult.getStringValue(valueDefId_cap, cap);
        if (!ok || cap.isEmpty() || cap.compare(QLatin1String("BUTT"), Qt::CaseInsensitive) == 0)
            paint.setStrokeCap(SkPaint::kButt_Cap);
        else if (cap.compare(QLatin1String("ROUND"), Qt::CaseInsensitive) == 0)
            paint.setStrokeCap(SkPaint::kRound_Cap);
        else if (cap.compare(QLatin1String("SQUARE"), Qt::CaseInsensitive) == 0)
            paint.setStrokeCap(SkPaint::kSquare_Cap);
        else
            paint.setStrokeCap(SkPaint::kButt_Cap);

        QString encodedPathEffect;
        ok = evalResult.getStringValue(valueDefId_pathEffect, encodedPathEffect);
        if (!ok || encodedPathEffect.isEmpty())
        {
            paint.setPathEffect(nullptr);
        }
        else
        {
            SkPathEffect* pathEffect = nullptr;
            ok = obtainPathEffect(encodedPathEffect, pathEffect);

            if (ok && pathEffect)
                paint.setPathEffect(pathEffect);
        }
    }

    SkColor color = SK_ColorTRANSPARENT;
    evalResult.getIntegerValue(valueDefId_color, color);
    paint.setColor(color);

    if (valueSetSelector == PaintValuesSet::Set_0)
    {
        QString shader;
        ok = evalResult.getStringValue(env->styleBuiltinValueDefs->id_OUTPUT_SHADER, shader);
        if (ok && !shader.isEmpty())
        {
            SkBitmapProcShader* shaderObj = nullptr;
            if (obtainBitmapShader(env, shader, shaderObj) && shaderObj)
            {
                // SKIA requires non-transparent color
                if (paint.getColor() == SK_ColorTRANSPARENT)
                    paint.setColor(SK_ColorWHITE);

                paint.setShader(static_cast<SkShader*>(shaderObj))->unref();
            }
        }
    }

    // do not check shadow color here
    if (context.shadowMode == MapPresentationEnvironment::ShadowMode::OneStep && valueSetSelector == PaintValuesSet::Set_0)
    {
        SkColor shadowColor = SK_ColorTRANSPARENT;
        ok = evalResult.getIntegerValue(env->styleBuiltinValueDefs->id_OUTPUT_SHADOW_COLOR, shadowColor);

        int shadowRadius = 0;
        evalResult.getIntegerValue(env->styleBuiltinValueDefs->id_OUTPUT_SHADOW_RADIUS, shadowRadius);

        if (!ok || shadowColor == SK_ColorTRANSPARENT)
            shadowColor = context.shadowColor.toSkColor();
        if (shadowColor == SK_ColorTRANSPARENT)
            shadowRadius = 0;

        if (shadowRadius > 0)
            paint.setLooper(SkBlurDrawLooper::Create(static_cast<SkScalar>(shadowRadius), 0, 0, shadowColor))->unref();
    }

    return true;
}

void OsmAnd::MapRasterizer_P::rasterizePolygon(
    const Context& context,
    SkCanvas& canvas,
    const std::shared_ptr<const MapPrimitiviser::Primitive>& primitive)
{
    const auto& points31 = primitive->sourceObject->points31;
    const auto& area31 = context.area31;

    assert(points31.size() > 2);
    assert(primitive->sourceObject->isClosedFigure());
    assert(primitive->sourceObject->isClosedFigure(true));

    //////////////////////////////////////////////////////////////////////////
    //if ((primitive->sourceObject->id >> 1) == 9223372032559801460u)
    //{
    //    int i = 5;
    //}
    //////////////////////////////////////////////////////////////////////////

    SkPaint paint = _defaultPaint;
    if (!updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_0, true))
        return;

    // Construct and test geometry against bbox area
    SkPath path;
    bool containsAtLeastOnePoint = false;
    int pointIdx = 0;
    PointF vertex;
    Utilities::CHValue prevChValue;
    QVector< PointI > outerPoints;
    const auto pointsCount = points31.size();
    auto pPoint = points31.constData();
    for (auto pointIdx = 0; pointIdx < pointsCount; pointIdx++, pPoint++)
    {
        const auto& point = *pPoint;
        calculateVertex(context, point, vertex);

        // Hit-test
        if (!containsAtLeastOnePoint)
        {
            if (area31.contains(point))
                containsAtLeastOnePoint = true;
            else
                outerPoints.push_back(point);

            const auto chValue = Utilities::computeCohenSutherlandValue(point, area31);
            if (Q_LIKELY(pointIdx > 0))
            {
                // Check if line crosses area (reject only if points are on the same side)
                const auto intersectedChValue = prevChValue & chValue;
                if (static_cast<unsigned int>(intersectedChValue) != 0)
                    containsAtLeastOnePoint = true;
            }
            prevChValue = chValue;
        }

        // Plot vertex
        if (pointIdx == 0)
            path.moveTo(vertex.x, vertex.y);
        else
            path.lineTo(vertex.x, vertex.y);
    }

    //////////////////////////////////////////////////////////////////////////
    //if ((primitive->sourceObject->id >> 1) == 9223372032559801460u)
    //{
    //    int i = 5;
    //}
    //////////////////////////////////////////////////////////////////////////

    if (!containsAtLeastOnePoint)
    {
        // Check area is inside polygon
        bool ok = true;
        ok = ok || containsHelper(outerPoints, area31.topLeft);
        ok = ok || containsHelper(outerPoints, area31.bottomRight);
        ok = ok || containsHelper(outerPoints, PointI(0, area31.bottom()));
        ok = ok || containsHelper(outerPoints, PointI(area31.right(), 0));
        if (!ok)
            return;
    }

    //////////////////////////////////////////////////////////////////////////
    //if ((primitive->sourceObject->id >> 1) == 95692962u)
    //{
    //    int i = 5;
    //}
    //////////////////////////////////////////////////////////////////////////

    if (!primitive->sourceObject->innerPolygonsPoints31.isEmpty())
    {
        path.setFillType(SkPath::kEvenOdd_FillType);
        for (const auto& polygon : constOf(primitive->sourceObject->innerPolygonsPoints31))
        {
            pointIdx = 0;
            for (auto itVertex = cachingIteratorOf(constOf(polygon)); itVertex; ++itVertex, pointIdx++)
            {
                const auto& point = *itVertex;
                calculateVertex(context, point, vertex);

                if (pointIdx == 0)
                    path.moveTo(vertex.x, vertex.y);
                else
                    path.lineTo(vertex.x, vertex.y);
            }
        }
    }

    canvas.drawPath(path, paint);
    if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_1, false))
        canvas.drawPath(path, paint);
}

void OsmAnd::MapRasterizer_P::rasterizePolyline(
    const Context& context,
    SkCanvas& canvas,
    const std::shared_ptr<const MapPrimitiviser::Primitive>& primitive,
    bool drawOnlyShadow)
{
    const auto& points31 = primitive->sourceObject->points31;
    const auto& area31 = context.area31;
    const auto& env = context.env;

    assert(points31.size() >= 2);

    SkPaint paint = _defaultPaint;
    if (!updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_0, false))
        return;

    bool ok;

    ColorARGB shadowColor;
    ok = primitive->evaluationResult.getIntegerValue(env->styleBuiltinValueDefs->id_OUTPUT_SHADOW_COLOR, shadowColor.argb);
    if (!ok || shadowColor == ColorARGB::fromSkColor(SK_ColorTRANSPARENT))
        shadowColor = context.shadowColor;

    int shadowRadius;
    ok = primitive->evaluationResult.getIntegerValue(env->styleBuiltinValueDefs->id_OUTPUT_SHADOW_RADIUS, shadowRadius);
    if (drawOnlyShadow && (!ok || shadowRadius == 0))
        return;

    const auto typeRuleId = primitive->sourceObject->typesRuleIds[primitive->typeRuleIdIndex];
    const auto& encDecRules = primitive->sourceObject->encodingDecodingRules;

    int oneway = 0;
    if (context.zoom >= ZoomLevel16 && typeRuleId == encDecRules->highway_encodingRuleId)
    {
        if (primitive->sourceObject->containsType(encDecRules->oneway_encodingRuleId, true))
            oneway = 1;
        else if (primitive->sourceObject->containsType(encDecRules->onewayReverse_encodingRuleId, true))
            oneway = -1;
    }

    SkPath path;
    int pointIdx = 0;
    bool intersect = false;
    int prevCross = 0;
    PointF vertex;
    const auto pointsCount = points31.size();
    auto pPoint = points31.constData();
    for (pointIdx = 0; pointIdx < pointsCount; pointIdx++, pPoint++)
    {
        const auto& point = *pPoint;
        calculateVertex(context, point, vertex);

        // Hit-test
        if (!intersect)
        {
            if (area31.contains(PointI(vertex)))
            {
                intersect = true;
            }
            else
            {
                int cross = 0;
                cross |= (point.x < area31.left() ? 1 : 0);
                cross |= (point.x > area31.right() ? 2 : 0);
                cross |= (point.y < area31.top() ? 4 : 0);
                cross |= (point.y > area31.bottom() ? 8 : 0);
                if (pointIdx > 0)
                {
                    if ((prevCross & cross) == 0)
                    {
                        intersect = true;
                    }
                }
                prevCross = cross;
            }
        }

        // Plot vertex
        if (pointIdx == 0)
            path.moveTo(vertex.x, vertex.y);
        else
            path.lineTo(vertex.x, vertex.y);
    }

    if (!intersect)
        return;

    if (pointIdx > 0)
    {
        if (drawOnlyShadow)
        {
            rasterizeLineShadow(context, canvas, path, paint, shadowColor, shadowRadius);
        }
        else
        {
            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_minus2, false))
                canvas.drawPath(path, paint);

            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_minus1, false))
                canvas.drawPath(path, paint);

            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_0, false))
                canvas.drawPath(path, paint);

            canvas.drawPath(path, paint);

            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_1, false))
                canvas.drawPath(path, paint);

            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_3, false))
                canvas.drawPath(path, paint);

            if (updatePaint(context, paint, primitive->evaluationResult, PaintValuesSet::Set_4, false))
                canvas.drawPath(path, paint);
            
            if (oneway && !drawOnlyShadow)
                rasterizeLine_OneWay(canvas, path, oneway);
        }
    }
}

void OsmAnd::MapRasterizer_P::rasterizeLineShadow(
    const Context& context,
    SkCanvas& canvas,
    const SkPath& path,
    SkPaint& paint,
    const ColorARGB shadowColor,
    int shadowRadius)
{
    if (context.shadowMode == MapPresentationEnvironment::ShadowMode::BlurShadow && shadowRadius > 0)
    {
        // simply draw shadow? difference from option 3 ?
        paint.setLooper(SkBlurDrawLooper::Create(shadowColor.toSkColor(), SkBlurMaskFilter::ConvertRadiusToSigma(shadowRadius), 0, 0))->unref();
        canvas.drawPath(path, paint);
    }
    else if (context.shadowMode == MapPresentationEnvironment::ShadowMode::SolidShadow && shadowRadius > 0)
    {
        paint.setLooper(nullptr);
        paint.setStrokeWidth(paint.getStrokeWidth() + shadowRadius * 2);
        paint.setColorFilter(SkColorFilter::CreateModeFilter(shadowColor.toSkColor(), SkXfermode::kSrcIn_Mode))->unref();
        canvas.drawPath(path, paint);
    }
}

void OsmAnd::MapRasterizer_P::rasterizeLine_OneWay(
    SkCanvas& canvas,
    const SkPath& path,
    int oneway)
{
    if (oneway > 0)
    {
        for (const auto& paint : constOf(_oneWayPaints))
            canvas.drawPath(path, paint);
    }
    else
    {
        for (const auto& paint : constOf(_reverseOneWayPaints))
            canvas.drawPath(path, paint);
    }
}

void OsmAnd::MapRasterizer_P::calculateVertex(
    const Context& context,
    const PointI& point31,
    PointF& vertex)
{
    vertex.x = static_cast<float>(point31.x - context.area31.left()) / context.primitivisedObjects->scaleDivisor31ToPixel.x;
    vertex.y = static_cast<float>(point31.y - context.area31.top()) / context.primitivisedObjects->scaleDivisor31ToPixel.y;
}

bool OsmAnd::MapRasterizer_P::containsHelper(const QVector< PointI >& points, const PointI& otherPoint)
{
    uint32_t intersections = 0;

    auto itPrevPoint = points.cbegin();
    auto itPoint = itPrevPoint + 1;
    for (const auto itEnd = points.cend(); itPoint != itEnd; itPrevPoint = itPoint, ++itPoint)
    {
        const auto& point0 = *itPrevPoint;
        const auto& point1 = *itPoint;

        if (Utilities::rayIntersect(point0, point1, otherPoint))
            intersections++;
    }

    // special handling, also count first and last, might not be closed, but
    // we want this!
    const auto& point0 = points.first();
    const auto& point1 = points.last();
    if (Utilities::rayIntersect(point0, point1, otherPoint))
        intersections++;

    return intersections % 2 == 1;
}

bool OsmAnd::MapRasterizer_P::obtainPathEffect(const QString& encodedPathEffect, SkPathEffect* &outPathEffect) const
{
    QMutexLocker scopedLocker(&_pathEffectsMutex);

    auto itPathEffects = _pathEffects.constFind(encodedPathEffect);
    if (itPathEffects == _pathEffects.cend())
    {
        const auto& strIntervals = encodedPathEffect.split(QLatin1Char('_'), QString::SkipEmptyParts);
        const auto intervalsCount = strIntervals.size();

        const auto intervals = new SkScalar[intervalsCount];
        auto pInterval = intervals;
        for (const auto& strInterval : constOf(strIntervals))
        {
            float computedValue = 0.0f;

            if (!strInterval.contains(QLatin1Char(':')))
            {
                computedValue = strInterval.toFloat()*owner->mapPresentationEnvironment->displayDensityFactor;
            }
            else
            {
                // "dip:px" format
                const auto& complexValue = strInterval.split(QLatin1Char(':'), QString::KeepEmptyParts);

                computedValue = complexValue[0].toFloat()*owner->mapPresentationEnvironment->displayDensityFactor + complexValue[1].toFloat();
            }

            *(pInterval++) = computedValue;
        }

        // Validate
        if (intervalsCount < 2 || intervalsCount % 2 != 0)
        {
            LogPrintf(LogSeverityLevel::Warning, "Path effect (%s) with %d intervals is invalid", qPrintable(encodedPathEffect), intervalsCount);
            return false;
        }

        SkPathEffect* pathEffect = SkDashPathEffect::Create(intervals, intervalsCount, 0);
        delete[] intervals;

        itPathEffects = _pathEffects.insert(encodedPathEffect, pathEffect);
    }

    outPathEffect = *itPathEffects;
    return true;
}

bool OsmAnd::MapRasterizer_P::obtainBitmapShader(const std::shared_ptr<const MapPresentationEnvironment>& env, const QString& name, SkBitmapProcShader* &outShader)
{
    std::shared_ptr<const SkBitmap> bitmap;
    if (!env->obtainShaderBitmap(name, bitmap))
    {
        LogPrintf(LogSeverityLevel::Warning,
            "Failed to get '%s' bitmap shader",
            qPrintable(name));

        return false;
    }

    outShader = new SkBitmapProcShader(*bitmap, SkShader::kRepeat_TileMode, SkShader::kRepeat_TileMode);
    return true;
}

OsmAnd::MapRasterizer_P::Context::Context(
    const AreaI area31_,
    const std::shared_ptr<const MapPrimitiviser::PrimitivisedObjects>& primitivisedObjects_)
    : area31(area31_)
    , primitivisedObjects(primitivisedObjects_)
    , env(primitivisedObjects->mapPresentationEnvironment)
    , zoom(primitivisedObjects->zoom)
{
    env->obtainShadowOptions(zoom, shadowMode, shadowColor);
}