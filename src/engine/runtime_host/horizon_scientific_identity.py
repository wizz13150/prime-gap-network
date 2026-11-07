"""Immutable qualified scientific component identities; no compute imports."""
from types import MappingProxyType

IMAGE_SHA='0A9706A9FA3A92AC49A7CFD343A342B8AFFFC9ECD5104639C7F0B2BBBE528D99'
SOURCE_SHA='09B1D9E48E92B902522320BE862C7A60CB5F93F5C69A1B9EAA4FE0D0E31B83C0'
TABLE_SHA='7C7E90ADCA242FD5625CAF0155BE63E19E0F8DA8B8E8FB3D3F3280C6BAB14ABC'
PRODUCER_SHA='FE68DEFB429C3225E144E7E0E4F7DE3D89F258BA1E74B2156C096C3080C4C945'
QUALIFIED_COMPONENTS=MappingProxyType({
    IMAGE_SHA:(SOURCE_SHA,86),
    '05AA2B85CDB11D0A4EBE950FEE2B1C55B0B314C50A1AD2F58C10E99555D6178E':
        ('512A3F83B873AB3014D8BF5B0BBA81BB9507DBEEFBAC5623318BBC78AFF6DDDF',86),
    # Same source bytes as the tested SM86 bucket worker; only the compiler
    # architecture and embedded architecture identity differ. Native SM120
    # hardware execution remains an explicit preview qualification obligation.
    '30AB021C38D5F452158ADAB55035129E854E7B56A43BC1A74F0C23D2C261D79E':
        ('77A8D7B3E3AFB4B7463AC3B8312B74992C7C17C27B201422AA895F0FCA0B81DC',120),
})
QUALIFIED_VARIANTS=MappingProxyType({image:record[0]for image,record in QUALIFIED_COMPONENTS.items()})


def qualified_complement_pair(image,source):
    return type(image)is str and type(source)is str and QUALIFIED_VARIANTS.get(image)==source


def qualified_complement_architecture(image,source):
    return QUALIFIED_COMPONENTS[image][1] if qualified_complement_pair(image,source) else None
