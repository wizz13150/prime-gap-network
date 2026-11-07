"""Optional public labels, bound to the existing immutable report commitment.

A label is an owner's self-declared pseudonym, not a unique identity, a verified
legal name, or independent evidence of work. It never changes scientific counts.
"""
import unicodedata

MAX_PUBLIC_NAME_UNITS = 48  # Match QString's UTF-16 length limit.


def public_name(value):
    if type(value) is not str or len(value.encode('utf-16-le', errors='surrogatepass')) > 2 * MAX_PUBLIC_NAME_UNITS:
        raise ValueError('Public name exceeds 48 UTF-16 characters')
    if any(unicodedata.category(c)[0] not in 'LMN' and c not in " ._'-" for c in value):
        raise ValueError('Unsupported public-name character')
    name = unicodedata.normalize('NFC', value).strip()
    if len(name.encode('utf-16-le')) > 2 * MAX_PUBLIC_NAME_UNITS:
        raise ValueError('Normalized public name exceeds capacity')
    if not name:
        return 'PGN'
    if not any(unicodedata.category(c)[0] in 'LN' for c in name):
        raise ValueError('Public name needs a letter or number')
    return name


def attribution(name):
    return dict(schema='PGN_PUBLIC_ATTRIBUTION_V1', name=public_name(name))


def validate_attribution(value):
    if (type(value) is not dict or set(value) != {'schema', 'name'}
            or value['schema'] != 'PGN_PUBLIC_ATTRIBUTION_V1'
            or public_name(value['name']) != value['name']):
        raise ValueError('Noncanonical public attribution')
    return value['name']
