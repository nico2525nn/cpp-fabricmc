package net.minecraft.util;

import net.minecraft.text.Text;

/** Progress callback used by the vanilla world-save ABI. */
public interface ProgressListener {
    default void progressStagePercentage(int percentage) { }
    default void setTitle(Text title) { }
}
