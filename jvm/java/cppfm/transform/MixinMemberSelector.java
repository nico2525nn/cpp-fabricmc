package cppfm.transform;

/** Parsed owner/member/quantifier/descriptor tuple used by Mixin selectors. */
public final class MixinMemberSelector {
    private enum OwnerFormat { NONE, INTERNAL, DOTTED }

    private final OwnerFormat ownerFormat;
    private final String owner;
    private final String member;
    private final String quantifier;
    private final String descriptor;
    private final boolean quantified;
    private final int minimumMatches;
    private final int maximumMatches;

    private MixinMemberSelector(OwnerFormat ownerFormat, String owner,
                                String member, String quantifier,
                                String descriptor, boolean quantified,
                                int minimumMatches, int maximumMatches) {
        this.ownerFormat = ownerFormat;
        this.owner = owner;
        this.member = member;
        this.quantifier = quantifier;
        this.descriptor = descriptor;
        this.quantified = quantified;
        this.minimumMatches = minimumMatches;
        this.maximumMatches = maximumMatches;
    }

    public static MixinMemberSelector parse(String selector) {
        String value = selector == null ? "" : selector.replaceAll("\\s", "");
        OwnerFormat ownerFormat = OwnerFormat.NONE;
        String owner = "";
        int selectorDescriptorStart = descriptorStart(value);
        int selectorMemberEnd = selectorDescriptorStart < 0
            ? value.length() : selectorDescriptorStart;
        int ownerSeparator = value.substring(0, selectorMemberEnd).lastIndexOf('.');
        if (ownerSeparator > 0) {
            String dottedOwner = value.substring(0, ownerSeparator);
            if (dottedOwner.startsWith("L"))
                throw new TransformException(
                    "malformed dotted Mixin owner; use an Lowner;member selector: " + selector);
            ownerFormat = OwnerFormat.DOTTED;
            owner = dottedOwner.replace('.', '/');
            value = value.substring(ownerSeparator + 1);
        } else if (value.startsWith("L")) {
            int ownerEnd = value.indexOf(';');
            if (ownerEnd > 1) {
                ownerFormat = OwnerFormat.INTERNAL;
                owner = value.substring(1, ownerEnd).replace('.', '/');
                value = value.substring(ownerEnd + 1);
            }
        }

        int descriptorStart = descriptorStart(value);
        String nameAndQuantifier = descriptorStart < 0
            ? value : value.substring(0, descriptorStart);
        String descriptor = descriptorStart < 0 ? null : value.substring(descriptorStart);
        Quantifier parsed = parseQuantifier(nameAndQuantifier);
        if (parsed.explicit && parsed.maximum == 0)
            throw new TransformException(
                "Mixin selector quantifier must allow at least one match: " + selector);
        String member = nameAndQuantifier.substring(0, parsed.start);
        String quantifier = nameAndQuantifier.substring(parsed.start);
        return new MixinMemberSelector(ownerFormat, owner, member, quantifier,
            descriptor, parsed.explicit, parsed.minimum, parsed.maximum);
    }

    public boolean hasOwner() { return ownerFormat != OwnerFormat.NONE; }
    public String owner() { return owner; }
    public String member() { return member; }
    public String descriptor() { return descriptor; }
    public boolean hasQuantifier() { return quantified; }
    public int minimumMatches() { return minimumMatches; }
    public int maximumMatches() { return maximumMatches; }

    public String formatMember(String mappedMember, String mappedDescriptor) {
        return mappedMember + quantifier + (mappedDescriptor == null ? "" : mappedDescriptor);
    }

    public String format(String mappedOwner, String mappedMember,
                         String mappedDescriptor) {
        String memberText = formatMember(mappedMember, mappedDescriptor);
        return switch (ownerFormat) {
            case NONE -> memberText;
            case INTERNAL -> "L" + mappedOwner + ";" + memberText;
            case DOTTED -> mappedOwner.replace('/', '.') + "." + memberText;
        };
    }

    private static int descriptorStart(String value) {
        int methodStart = value.indexOf('(');
        int fieldStart = value.indexOf(':');
        if (methodStart < 0) return fieldStart;
        if (fieldStart < 0) return methodStart;
        return Math.min(methodStart, fieldStart);
    }

    private static Quantifier parseQuantifier(String value) {
        int end = value.length();
        if (end == 0) return Quantifier.none(end);
        char last = value.charAt(end - 1);
        if (last == '*') return new Quantifier(end - 1, true, 0, Integer.MAX_VALUE);
        if (last == '+') return new Quantifier(end - 1, true, 1, Integer.MAX_VALUE);
        if (last != '}') {
            if (value.indexOf('{') >= 0)
                throw malformedQuantifier(value);
            return Quantifier.none(end);
        }

        int open = value.lastIndexOf('{');
        if (open < 0) throw malformedQuantifier(value);
        String range = value.substring(open + 1, end - 1);
        int comma = range.indexOf(',');
        try {
            if (comma < 0) {
                int exact = parseCount(range);
                return new Quantifier(open, true, exact, exact);
            }
            if (range.indexOf(',', comma + 1) >= 0)
                throw malformedQuantifier(value);
            String minimumText = range.substring(0, comma);
            String maximumText = range.substring(comma + 1);
            int minimum = minimumText.isEmpty() ? 0 : parseCount(minimumText);
            int maximum = maximumText.isEmpty()
                ? Integer.MAX_VALUE : parseCount(maximumText);
            return new Quantifier(open, true, minimum,
                Math.max(minimum, maximum));
        } catch (NumberFormatException invalidRange) {
            throw malformedQuantifier(value);
        }
    }

    private static TransformException malformedQuantifier(String selector) {
        return new TransformException(
            "malformed Mixin selector quantifier: " + selector);
    }

    private static int parseCount(String value) {
        if (value.isEmpty()) throw new NumberFormatException("empty selector count");
        for (int index = 0; index < value.length(); ++index)
            if (!Character.isDigit(value.charAt(index)))
                throw new NumberFormatException("non-numeric selector count");
        return Integer.parseInt(value);
    }

    private record Quantifier(int start, boolean explicit,
                              int minimum, int maximum) {
        static Quantifier none(int start) {
            return new Quantifier(start, false, 0, 1);
        }
    }
}
