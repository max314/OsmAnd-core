#include "ObfMapObjectsMetricsLayerProvider.h"
#include "ObfMapObjectsMetricsLayerProvider_P.h"

OsmAnd::ObfMapObjectsMetricsLayerProvider::ObfMapObjectsMetricsLayerProvider(
    const std::shared_ptr<ObfMapObjectsProvider>& dataProvider_,
    const uint32_t tileSize_ /*= 256*/,
    const float densityFactor_ /*= 1.0f*/)
    : _p(new ObfMapObjectsMetricsLayerProvider_P(this))
    , dataProvider(dataProvider_)
    , tileSize(tileSize_)
    , densityFactor(densityFactor_)
{
}

OsmAnd::ObfMapObjectsMetricsLayerProvider::~ObfMapObjectsMetricsLayerProvider()
{
}

OsmAnd::MapStubStyle OsmAnd::ObfMapObjectsMetricsLayerProvider::getDesiredStubsStyle() const
{
    return MapStubStyle::Unspecified;
}

float OsmAnd::ObfMapObjectsMetricsLayerProvider::getTileDensityFactor() const
{
    return densityFactor;
}

uint32_t OsmAnd::ObfMapObjectsMetricsLayerProvider::getTileSize() const
{
    return tileSize;
}

bool OsmAnd::ObfMapObjectsMetricsLayerProvider::obtainData(
    const TileId tileId,
    const ZoomLevel zoom,
    std::shared_ptr<IMapTiledDataProvider::Data>& outTiledData,
    std::shared_ptr<Metric>* pOutMetric /*= nullptr*/,
    const IQueryController* const queryController /*= nullptr*/)
{
    if (pOutMetric)
        pOutMetric->reset();

    std::shared_ptr<Data> tiledData;
    const auto result = _p->obtainData(tileId, zoom, tiledData, queryController);
    outTiledData = tiledData;

    return result;
}

OsmAnd::ZoomLevel OsmAnd::ObfMapObjectsMetricsLayerProvider::getMinZoom() const
{
    return _p->getMinZoom();
}

OsmAnd::ZoomLevel OsmAnd::ObfMapObjectsMetricsLayerProvider::getMaxZoom() const
{
    return _p->getMaxZoom();
}

OsmAnd::IMapDataProvider::SourceType OsmAnd::ObfMapObjectsMetricsLayerProvider::getSourceType() const
{
    const auto underlyingSourceType = dataProvider->getSourceType();

    switch (underlyingSourceType)
    {
        case IMapDataProvider::SourceType::LocalDirect:
        case IMapDataProvider::SourceType::LocalGenerated:
            return IMapDataProvider::SourceType::LocalGenerated;

        case IMapDataProvider::SourceType::NetworkDirect:
        case IMapDataProvider::SourceType::NetworkGenerated:
            return IMapDataProvider::SourceType::NetworkGenerated;

        case IMapDataProvider::SourceType::MiscDirect:
        case IMapDataProvider::SourceType::MiscGenerated:
        default:
            return IMapDataProvider::SourceType::MiscGenerated;
    }
}

OsmAnd::ObfMapObjectsMetricsLayerProvider::Data::Data(
    const TileId tileId_,
    const ZoomLevel zoom_,
    const AlphaChannelPresence alphaChannelPresence_,
    const float densityFactor_,
    const std::shared_ptr<const SkBitmap>& bitmap_,
    const std::shared_ptr<const ObfMapObjectsProvider::Data>& binaryMapData_,
    const RetainableCacheMetadata* const pRetainableCacheMetadata_ /*= nullptr*/)
    : IRasterMapLayerProvider::Data(tileId_, zoom_, alphaChannelPresence_, densityFactor_, bitmap_, pRetainableCacheMetadata_)
    , binaryMapData(binaryMapData_)
{
}

OsmAnd::ObfMapObjectsMetricsLayerProvider::Data::~Data()
{
    release();
}
