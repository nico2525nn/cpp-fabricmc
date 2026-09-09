package net.fabricmc.fabric.api.blockview.v2;

/** Optional block-entity render-data provider. */
public interface RenderDataBlockEntity {
    default Object getRenderData() { return null; }
}
