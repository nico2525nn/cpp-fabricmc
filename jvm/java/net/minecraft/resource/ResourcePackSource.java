package net.minecraft.resource;

import java.util.Objects;
import java.util.function.UnaryOperator;
import net.minecraft.text.Text;

/** Origin and enablement policy for a resource pack. */
public final class ResourcePackSource {
    public static final ResourcePackSource NONE = create(UnaryOperator.identity(), false);
    public static final ResourcePackSource BUILTIN = create(source -> source, false);
    public static final ResourcePackSource WORLD = create(source -> source, true);
    public static final ResourcePackSource SERVER = create(source -> source, true);
    public static final ResourcePackSource FEATURE = create(source -> source, true);
    private static final UnaryOperator<Text> NONE_SOURCE_TEXT_SUPPLIER = UnaryOperator.identity();

    private final UnaryOperator<Text> sourceTextSupplier;
    private final boolean canBeEnabledLater;

    private ResourcePackSource(UnaryOperator<Text> sourceTextSupplier, boolean canBeEnabledLater) {
        this.sourceTextSupplier = Objects.requireNonNull(sourceTextSupplier, "sourceTextSupplier");
        this.canBeEnabledLater = canBeEnabledLater;
    }

    public static ResourcePackSource create(UnaryOperator<Text> sourceTextSupplier, boolean canBeEnabledLater) {
        return new ResourcePackSource(sourceTextSupplier, canBeEnabledLater);
    }

    public static UnaryOperator<Text> getSourceTextSupplier(String translationKey) {
        return text -> text == null ? Text.empty() : Text.translatable(translationKey, text);
    }

    public boolean canBeEnabledLater() { return canBeEnabledLater; }
    public Text decorate(Text packDisplayName) { return sourceTextSupplier.apply(packDisplayName); }
    public Text method_45283(Text name, Text ignored) { return decorate(name); }
}
