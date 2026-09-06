package net.minecraft.world;

import net.minecraft.util.math.BlockPos;

/**
 * 1.21.4 vertical world-height contract shared by worlds and chunks.
 * Native storage remains authoritative; these defaults keep the Java shadow
 * ABI useful to mixins which only query height arithmetic.
 */
public interface HeightLimitView {
    int getBottomY();
    int getHeight();

    default int getTopYInclusive() {
        return getBottomY() + getHeight() - 1;
    }

    default int getBottomSectionCoord() {
        return Math.floorDiv(getBottomY(), 16);
    }

    default int getTopSectionCoord() {
        return Math.floorDiv(getTopYInclusive(), 16) + 1;
    }

    default int countVerticalSections() {
        return getTopSectionCoord() - getBottomSectionCoord();
    }

    default int getSectionIndex(int y) {
        return sectionCoordToIndex(Math.floorDiv(y, 16));
    }

    default int sectionCoordToIndex(int coord) {
        return coord - getBottomSectionCoord();
    }

    default int sectionIndexToCoord(int index) {
        return index + getBottomSectionCoord();
    }

    default boolean isOutOfHeightLimit(int y) {
        return y < getBottomY() || y > getTopYInclusive();
    }

    default boolean isOutOfHeightLimit(BlockPos pos) {
        return pos == null || isOutOfHeightLimit(pos.getY());
    }

    default boolean isInHeightLimit(int y) {
        return !isOutOfHeightLimit(y);
    }

    default int getTopY() {
        return getBottomY() + getHeight();
    }

    static HeightLimitView create(int bottomY, int height) {
        if (height <= 0) throw new IllegalArgumentException("height must be positive");
        return new HeightLimitView() {
            @Override public int getBottomY() { return bottomY; }
            @Override public int getHeight() { return height; }
        };
    }
}
