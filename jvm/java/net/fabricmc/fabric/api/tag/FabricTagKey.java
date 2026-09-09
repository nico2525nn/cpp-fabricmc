package net.fabricmc.fabric.api.tag;

import net.minecraft.registry.tag.TagKey;
import net.minecraft.text.Text;

/** Additional name helpers injected into vanilla TagKey by Fabric. */
public interface FabricTagKey {
    default String getTranslationKey() {
        if (this instanceof TagKey<?> tag) {
            return "tag." + tag.id().getNamespace() + "." + tag.id().getPath().replace('/', '.');
        }
        return "tag.unknown";
    }

    default Text getName() {
        return Text.translatable(getTranslationKey());
    }
}
