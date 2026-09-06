package net.fabricmc.loader.api.metadata;

/** JSON-like custom metadata value exposed by Fabric Loader. */
public interface CustomValue {
    CvType getType();
    CvObject getAsObject();
    CvArray getAsArray();
    String getAsString();
    Number getAsNumber();
    boolean getAsBoolean();

    enum CvType { OBJECT, ARRAY, STRING, NUMBER, BOOLEAN, NULL }

    interface CvObject extends Iterable<java.util.Map.Entry<String, CustomValue>>, CustomValue {
        int size();
        boolean containsKey(String key);
        CustomValue get(String key);
    }

    interface CvArray extends Iterable<CustomValue>, CustomValue {
        int size();
        CustomValue get(int index);
    }
}
