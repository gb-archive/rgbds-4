PURGE _RS
PRINTLN _RS

DEF _RS EQU 1
PRINTLN _RS

DEF _RS = 2 ; this works
PRINTLN _RS

DEF _RS EQUS "3"
PRINTLN _RS

REDEF _RS = 4 ; this works
PRINTLN _RS

REDEF _RS EQU 5
PRINTLN _RS

REDEF _RS EQUS "6"
PRINTLN _RS

RSSET 7 ; this works
PRINTLN _RS

RSRESET
PRINTLN _RS