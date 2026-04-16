save_handler:
    movem.l %d0-%d7/%a0-%a6,%a7@-
    move.l %usp, %a6
    move.l %a6, %a7@-

    lea 0xff0000, %a6
    move.l %a7, %a6@
    
    move.l %a7@+, %a6
    move.l %a6, %usp
    movem.l %a7@+, %d0-%d7/%a0-%a6
    rte



