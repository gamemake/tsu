
set FILE_ORG=%1
set FILE_LIB=%FILE_ORG:.dll.lib=.lib%
set FILE_PDB=%FILE_ORG:.dll.pdb=.pdb%

if "%FILE_ORG%" neq "%FILE_LIB%" (
    move "%FILE_ORG%" "%FILE_LIB%"
)

if "%FILE_ORG%" neq "%FILE_PDB%" (
    move "%FILE_ORG%" "%FILE_PDB%"
)
