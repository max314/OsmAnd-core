#include "MapRendererElevationDataResource.h"

#include "IMapElevationDataProvider.h"
#include "MapRendererResourcesManager.h"

OsmAnd::MapRendererElevationDataResource::MapRendererElevationDataResource(
    MapRendererResourcesManager* owner_,
    const IMapDataProvider::SourceType sourceType_,
    const TiledEntriesCollection<MapRendererBaseTiledResource>& collection_,
    const TileId tileId_,
    const ZoomLevel zoom_)
    : MapRendererBaseTiledResource(owner_, MapRendererResourceType::ElevationData, sourceType_, collection_, tileId_, zoom_)
    , resourceInGPU(_resourceInGPU)
{
}

OsmAnd::MapRendererElevationDataResource::~MapRendererElevationDataResource()
{
    safeUnlink();
}

bool OsmAnd::MapRendererElevationDataResource::obtainData(bool& dataAvailable, const IQueryController* queryController)
{
    bool ok = false;

    // Get source of tile
    std::shared_ptr<IMapDataProvider> provider_;
    if (const auto link_ = link.lock())
    {
        ok = resourcesManager->obtainProviderFor(
            static_cast<MapRendererBaseResourcesCollection*>(static_cast<MapRendererTiledResourcesCollection*>(&link_->collection)),
            provider_);
    }
    if (!ok)
        return false;
    const auto provider = std::static_pointer_cast<IMapTiledDataProvider>(provider_);

    // Obtain tile from provider
    std::shared_ptr<IMapTiledDataProvider::Data> tile;
    const auto requestSucceeded = provider->obtainData(tileId, zoom, tile, nullptr, queryController);
    if (!requestSucceeded)
        return false;
    dataAvailable = static_cast<bool>(tile);

    // Store data
    if (dataAvailable)
        _sourceData = std::static_pointer_cast<IMapElevationDataProvider::Data>(tile);

    return true;
}

bool OsmAnd::MapRendererElevationDataResource::uploadToGPU()
{
    bool ok = resourcesManager->uploadTiledDataToGPU(_sourceData, _resourceInGPU);
    if (!ok)
        return false;

    // Since content was uploaded to GPU, it's safe to release it keeping retainable data
    _retainableCacheMetadata = _sourceData->retainableCacheMetadata;
    _sourceData.reset();

    return true;
}

void OsmAnd::MapRendererElevationDataResource::unloadFromGPU()
{
    _resourceInGPU.reset();
}

void OsmAnd::MapRendererElevationDataResource::lostDataInGPU()
{
    _resourceInGPU->lostRefInGPU();
    _resourceInGPU.reset();
}

void OsmAnd::MapRendererElevationDataResource::releaseData()
{
    _retainableCacheMetadata.reset();
    _sourceData.reset();
}
