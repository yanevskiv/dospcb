@echo off
PATH C:\BC31\BIN
CLS
make test > ERR.TXT
type ERR.TXT