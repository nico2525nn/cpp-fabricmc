package net.minecraft.network.message;

/** Value object for the signed-message shape used by Fabric server callbacks. */
public final class SignedMessage {
    private final String content;
    private final boolean signed;
    public SignedMessage(String content) { this(content, true); }
    public SignedMessage(String content, boolean signed) {
        this.content = content == null ? "" : content;
        this.signed = signed;
    }
    public static SignedMessage of(String content) { return new SignedMessage(content, true); }
    public String getContent() { return content; }
    public String getSignedContent() { return content; }
    public boolean isSigned() { return signed; }
    public boolean isEmpty() { return content.isEmpty(); }
    @Override public String toString() { return content; }
}
