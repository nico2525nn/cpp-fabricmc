package net.fabricmc.fabric.api.resource;

import net.minecraft.util.Identifier;

/** Vanilla reload phase identifiers exposed by Fabric resource-loader. */
public final class ResourceReloadListenerKeys {
    public static final Identifier SOUNDS = Identifier.ofVanilla("sounds");
    public static final Identifier FONTS = Identifier.ofVanilla("fonts");
    public static final Identifier MODELS = Identifier.ofVanilla("models");
    public static final Identifier LANGUAGES = Identifier.ofVanilla("languages");
    public static final Identifier TEXTURES = Identifier.ofVanilla("textures");
    public static final Identifier RECIPES = Identifier.ofVanilla("recipes");
    public static final Identifier ADVANCEMENTS = Identifier.ofVanilla("advancements");
    public static final Identifier FUNCTIONS = Identifier.ofVanilla("functions");

    private ResourceReloadListenerKeys() { }
}
