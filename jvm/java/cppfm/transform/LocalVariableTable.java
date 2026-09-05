package cppfm.transform;

import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;

/**
 * Decoded LocalVariableTable metadata for a single Code attribute.
 *
 * <p>The verifier-derived state in {@link StackAnalyzer} is authoritative for
 * whether a slot is live.  This table supplies the source-level descriptor
 * when the class file contains debug locals, which is important for reference
 * locals whose bytecode opcode alone only says {@code aload}/{@code astore}.
 * Missing debug metadata is deliberately allowed; callers fall back to the
 * conservative verifier state.</p>
 */
final class LocalVariableTable {
    private final List<Entry> entries;

    private LocalVariableTable(List<Entry> entries) {
        this.entries = List.copyOf(entries);
    }

    static LocalVariableTable read(CodeModel code, ConstantPool pool) {
        ArrayList<Entry> entries = new ArrayList<>();
        for (AttributeModel attribute : code.attributes) {
            if (!attribute.name(pool).equals("LocalVariableTable")) continue;
            try {
                DataInputStream input = new DataInputStream(new ByteArrayInputStream(attribute.info));
                int count = input.readUnsignedShort();
                for (int i = 0; i < count; ++i) {
                    int start = input.readUnsignedShort();
                    int length = input.readUnsignedShort();
                    String name = pool.utf8(input.readUnsignedShort());
                    String descriptor = pool.utf8(input.readUnsignedShort());
                    int slot = input.readUnsignedShort();
                    entries.add(new Entry(start, length, slot, name, descriptor));
                }
                if (input.available() != 0) throw new TransformException("trailing LocalVariableTable data");
            } catch (IOException | RuntimeException failure) {
                if (failure instanceof TransformException exception) throw exception;
                throw new TransformException("invalid LocalVariableTable", failure);
            }
        }
        entries.sort(Comparator.comparingInt(Entry::slot)
            .thenComparingInt(Entry::start)
            .thenComparingInt(Entry::length));
        return new LocalVariableTable(entries);
    }

    /** Return the narrowest live debug entry for a slot at a bytecode offset. */
    Entry at(int offset, int slot) {
        Entry selected = null;
        for (Entry entry : entries) {
            if (entry.slot != slot || !entry.contains(offset)) continue;
            if (selected == null || entry.length < selected.length) selected = entry;
        }
        return selected;
    }

    record Entry(int start, int length, int slot, String name, String descriptor) {
        boolean contains(int offset) {
            return offset >= start && offset < start + length;
        }
    }
}
