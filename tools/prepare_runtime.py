"""Build helper: inputs have been hash-verified by Setup-Runtime.ps1."""
import pathlib
import sys
import zipfile

destination=pathlib.Path(sys.argv[2]).resolve()
with zipfile.ZipFile(sys.argv[1]) as package:
    for entry in package.infolist():
        if not (destination/entry.filename).resolve().is_relative_to(destination):
            raise ValueError('Wheel contains an unsafe path')
    package.extractall(destination)
sys.path.insert(0,str(destination))
import frida
assert frida.__version__=='17.23.3'
print('Pinned capture runtime verified')
