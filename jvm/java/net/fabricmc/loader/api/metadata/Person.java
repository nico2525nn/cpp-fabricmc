package net.fabricmc.loader.api.metadata;

/** A mod author or contributor. */
public interface Person {
    String getName();
    ContactInformation getContact();
}
