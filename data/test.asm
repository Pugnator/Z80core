; 24.03.2015
; Test Z80 assembly file
.org 0
start:
djnz end
ld d, end
end:
.end

After .end directive one can place anything
