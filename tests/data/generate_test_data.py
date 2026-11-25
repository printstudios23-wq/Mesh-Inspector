#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Writes the 3MF test files used by tests/test_main.cpp (plain Python 3).

  cube.3mf            deflate-compressed, one object, build item translated by +10 in X
  cube_stored.3mf     same model, but stored (no compression)
  components.3mf      production-extension layout: the cube lives in a second model file and is
                      placed twice by components, once mirrored in X (must still be a valid solid)
"""
import os, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
VERTS = [(0,0,0),(1,0,0),(1,1,0),(0,1,0),(0,0,1),(1,0,1),(1,1,1),(0,1,1)]
TRIS = [(0,3,2),(0,2,1),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(2,3,7),(2,7,6),(0,4,7),(0,7,3),(1,2,6),(1,6,5)]

def mesh_xml(indent="   "):
    v = "".join('%s  <vertex x="%g" y="%g" z="%g"/>\n' % ((indent,)+p) for p in VERTS)
    t = "".join('%s  <triangle v1="%d" v2="%d" v3="%d"/>\n' % ((indent,)+f) for f in TRIS)
    return "%s<mesh>\n%s<vertices>\n%s%s</vertices>\n%s<triangles>\n%s%s</triangles>\n%s</mesh>\n" % (indent, indent, v, indent, indent, t, indent, indent)

RELS = '''<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>'''
CT = '''<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>'''

def write(name, files, method):
    with zipfile.ZipFile(os.path.join(HERE, name), "w", method) as z:
        for n, data in files.items():
            z.writestr(n, data)

simple = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
          ' <resources>\n  <object id="1" type="model">\n' + mesh_xml() + '  </object>\n </resources>\n'
          ' <build>\n  <item objectid="1" transform="1 0 0 0 1 0 0 0 1 10 0 0"/>\n </build>\n</model>\n')
files = {"[Content_Types].xml": CT, "_rels/.rels": RELS, "3D/3dmodel.model": simple}
write("cube.3mf", files, zipfile.ZIP_DEFLATED)
write("cube_stored.3mf", files, zipfile.ZIP_STORED)

part = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
        ' <resources>\n  <object id="1" type="model">\n' + mesh_xml() + '  </object>\n </resources>\n <build/>\n</model>\n')
main = ('<?xml version="1.0" encoding="UTF-8"?>\n<model unit="millimeter" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02" '
        'xmlns:p="http://schemas.microsoft.com/3dmanufacturing/production/2015/06">\n'
        ' <resources>\n  <object id="2" type="model">\n   <components>\n'
        '    <component p:path="/3D/Objects/part.model" objectid="1" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n'
        '    <component p:path="/3D/Objects/part.model" objectid="1" transform="-1 0 0 0 1 0 0 0 1 -5 0 0"/>\n'
        '   </components>\n  </object>\n </resources>\n'
        ' <build>\n  <item objectid="2" transform="1 0 0 0 1 0 0 0 1 0 0 0"/>\n </build>\n</model>\n')
write("components.3mf", {"[Content_Types].xml": CT, "_rels/.rels": RELS, "3D/3dmodel.model": main, "3D/Objects/part.model": part}, zipfile.ZIP_DEFLATED)
print("written to", HERE)
