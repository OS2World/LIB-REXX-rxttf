
rxttf.dll:     rxttf.obj  rxttf.def
         ILINK /NOFREE $*.obj,$*.dll,,REXX ..\lib\libttf,$*.def;

rxttf.obj:     rxttf.c
         icc -c -Ge- -Gm+ -O+ -I..\lib $*.c


