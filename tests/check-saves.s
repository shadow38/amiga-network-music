| Self-test of tools/check-saves.py (make test): each _bad_ function writes a callee-saved register without
| saving it, in a way the script must see; the _good_ ones save what they write, or write only scratch registers.
#NO_APP
	.text
	.globl	_bad_postinc
_bad_postinc:
	move.l 4(sp),a2
	move.l (a2)+,d0
	rts
	.globl	_bad_predec
_bad_predec:
	clr.l -(a3)
	rts
	.globl	_bad_tst_postinc
_bad_tst_postinc:
	tst.l (a4)+
	rts
	.globl	_bad_dbra
_bad_dbra:
	moveq #9,d0
.L1:
	dbra d3,.L1
	rts
	.globl	_bad_plain
_bad_plain:
	moveq #0,d2
	rts
	.globl	_bad_fpu
_bad_fpu:
	fmove.d 4(sp),fp0
	fmove.x fp0,fp2
	fadd.x fp2,fp0
	rts
	.globl	_good_fpu
_good_fpu:
	fmovem #28,-(sp)
	fmove.x fp2,-(sp)
	fmove.d 40(sp),fp2
	fmul.x fp2,fp3
	fmove.x fp3,fp4
	fcmp.x fp5,fp0
	ftst.x fp6
	fmove.x (sp)+,fp2
	fmovem (sp)+,#56
	rts
	.globl	_good_saved
_good_saved:
	movem.l d3/a2-a3,-(sp)
	move.l 16(sp),a2
	move.l (a2)+,d0
	clr.l -(a3)
	moveq #3,d3
.L2:
	dbra d3,.L2
	movem.l (sp)+,d3/a2-a3
	rts
	.globl	_good_scratch
_good_scratch:
	move.l 4(sp),a0
	move.l (a0)+,d0
	move.l (a2),d1
	move.l d0,(a3)
	dbra d1,.L3
.L3:
	rts
