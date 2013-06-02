#
# Switching code for PS3.
# Save everyting that isn't volatile
# See the PPU ABI Specs for CELL OS lv-2, Low Level system information for details
# Kristjan Valur Jonsson, March 2010
#

###CODE SECTION###
.text

##MAIN ENTRY POINT
.globl .slp_switch
.type .slp_switch,@function
.slp_switch:
	#Save all 18 volatile GP registers, 18 volatile FP regs, and 12 volatile vector regs
	#We need a stack frame of 144 bytes for FPR, 144 bytes for GPR, 192 bytes for VR
	#plus 48 bytes for the standard stackframe = 528 bytes (which is quadword dividable)
	mflr  %r0                    # Move LR into r0
	subi  %r12,%r1,144           # Set r12 to general register save area (8*18)
	bl    _savegpr1_14           # Call routine to save general registers
	bl    _savefpr_14            # Call routine to save floating-point registers
	subi  %r0,%r1, 288		     # Set r0 to first address beyond vector save area
	bl    _savevr_20			 # Call routine to save vector registers
	stdu  %r1,(-528)(%r1)		 # Create stack frame
	
	#(save CR if necessary)
	mfcr  %r12
	std   %r12,8(%r1)			 #save it in the condition save area
	
	#Call slp_save_state with stack pointer
	addi  %r3,%r1,0				 # move stack pointer to r2
	bl	.slp_save_state		
	nop#
	extsw	%r3,%r3				 # extend sign bit to 64
	
	#check the low order bit
	addi	%r4,%r0,1			 # load 1
	and	    %r4,%r3,%r4			 # and
	cmpi	0,%r4,0
	bne-	0, NO_RESTORE		 # don't restore the stack (branch usually not taken)
		
	add		%r1,%r1,%r3			 # adjust stack pointer
	
	#Restore stack.
	#now, we have to be careful.  The function call will store the link register
	#in the current frame (as the ABI) dictates.  But it will then trample it
	#with the restore!  We fix this by creating a fake stack frame	
	stdu  %r1,(-48)(%r1)		 # Create fake stack frame for the link register storage
	bl	    .slp_restore_state   # Restore state into new stack
	nop#
	addi    %sp,%sp,48  		 # restore proper stack frame
	
	addi    %r3,%r0,0			 # set the return value (0)
	
	#restore the condition register
	ld      %r12,8(%r1)
	mtcrf   0xff, %r12
	
	#restore stack pointer
	addi	%sp,%sp,528
	
	#restore vector registers
	subi  %r0,%r1, 288		     # Set r0 to first address beyond vector save area
	bl    _savevr_20			 # Call routine to save vector registers
	
	#restore general purporse registers
	subi  %r12,%r1,144           # Set r12 to general register save area (8*18)
	bl    _restgpr1_14           # Restore general registers
	b	  _restfpr_14			 # Restore floating point regs and return

NO_RESTORE:
	#are we -1 (error)?
	cmpi	0,%r3,-1
	beq- 	0, RETURN_NO_RESTORE  # return with the -1 already in r3
	
	#no error
	addi	%r3,%r0,0
	
RETURN_NO_RESTORE:
    addi    %sp,%sp,528
    ld      %r0, 16(%r1)
    mtlr    %r0
    blr


#Stack saving and restoring functions from the ABI documentation.  The ABI states that these should
#be part of the ABI, but the linker refuses to find them :)

_savegpr1_14:  std  %r14,-144(%r12)
_savegpr1_15:  std  %r15,-136(%r12)
_savegpr1_16:  std  %r16,-128(%r12)
_savegpr1_17:  std  %r17,-120(%r12)
_savegpr1_18:  std  %r18,-112(%r12)
_savegpr1_19:  std  %r19,-104(%r12)
_savegpr1_20:  std  %r20,-96(%r12)
_savegpr1_21:  std  %r21,-88(%r12)
_savegpr1_22:  std  %r22,-80(%r12)
_savegpr1_23:  std  %r23,-72(%r12)
_savegpr1_24:  std  %r24,-64(%r12)
_savegpr1_25:  std  %r25,-56(%r12)
_savegpr1_26:  std  %r26,-48(%r12)
_savegpr1_27:  std  %r27,-40(%r12)
_savegpr1_28:  std  %r28,-32(%r12)
_savegpr1_29:  std  %r29,-24(%r12)
_savegpr1_30:  std  %r30,-16(%r12)
_savegpr1_31:  std  %r31,-8(%r12)
               blr

_restgpr1_14:  ld   %r14,-144(%r12)
_restgpr1_15:  ld   %r15,-136(%r12)
_restgpr1_16:  ld   %r16,-128(%r12)
_restgpr1_17:  ld   %r17,-120(%r12)
_restgpr1_18:  ld   %r18,-112(%r12)
_restgpr1_19:  ld   %r19,-104(%r12)
_restgpr1_20:  ld   %r20,-96(%r12)
_restgpr1_21:  ld   %r21,-88(%r12)
_restgpr1_22:  ld   %r22,-80(%r12)
_restgpr1_23:  ld   %r23,-72(%r12)
_restgpr1_24:  ld   %r24,-64(%r12)
_restgpr1_25:  ld   %r25,-56(%r12)
_restgpr1_26:  ld   %r26,-48(%r12)
_restgpr1_27:  ld   %r27,-40(%r12)
_restgpr1_28:  ld   %r28,-32(%r12)
_restgpr1_29:  ld   %r29,-24(%r12)
_restgpr1_30:  ld   %r30,-16(%r12)
_restgpr1_31:  ld   %r31,-8(%r12)
               blr

_savefpr_14:  stfd %f14,-144(%r1)
_savefpr_15:  stfd %f15,-136(%r1)
_savefpr_16:  stfd %f16,-128(%r1)
_savefpr_17:  stfd %f17,-120(%r1)
_savefpr_18:  stfd %f18,-112(%r1)
_savefpr_19:  stfd %f19,-104(%r1)
_savefpr_20:  stfd %f20,-96(%r1)
_savefpr_21:  stfd %f21,-88(%r1)
_savefpr_22:  stfd %f22,-80(%r1)
_savefpr_23:  stfd %f23,-72(%r1)
_savefpr_24:  stfd %f24,-64(%r1)
_savefpr_25:  stfd %f25,-56(%r1)
_savefpr_26:  stfd %f26,-48(%r1)
_savefpr_27:  stfd %f27,-40(%r1)
_savefpr_28:  stfd %f28,-32(%r1)
_savefpr_29:  stfd %f29,-24(%r1)
_savefpr_30:  stfd %f30,-16(%r1)
_savefpr_31:  stfd %f31,-8(%r1)
              std  %r0, 16(%r1)
              blr

_restfpr_14:  lfd  %f14,-144(%r1)
_restfpr_15:  lfd  %f15,-136(%r1)
_restfpr_16:  lfd  %f16,-128(%r1)
_restfpr_17:  lfd  %f17,-120(%r1)
_restfpr_18:  lfd  %f18,-112(%r1)
_restfpr_19:  lfd  %f19,-104(%r1)
_restfpr_20:  lfd  %f20,-96(%r1)
_restfpr_21:  lfd  %f21,-88(%r1)
_restfpr_22:  lfd  %f22,-80(%r1)
_restfpr_23:  lfd  %f23,-72(%r1)
_restfpr_24:  lfd  %f24,-64(%r1)
_restfpr_25:  lfd  %f25,-56(%r1)
_restfpr_26:  lfd  %f26,-48(%r1)
_restfpr_27:  lfd  %f27,-40(%r1)
_restfpr_28:  lfd  %f28,-32(%r1)
_restfpr_29:  ld   %r0, 16(%r1)
              lfd  %f29,-24(%r1)
              mtlr %r0
              lfd  %f30,-16(%r1)
              lfd  %f31,-8(%r1)
              blr
_restfpr_30:  lfd  %f30,-16(%r1)
_restfpr_31:  ld   %r0, 16(%r1)
              lfd  %f31,-8(%r1)
              mtlr %r0
              blr

_savevr_20:   addi %r12,%r0,-192
              stvx %v20,%r12,%r0
_savevr_21:   addi %r12,%r0,-176
              stvx %v21,%r12,%r0
_savevr_22:   addi %r12,%r0,-160
              stvx %v22,%r12,%r0
_savevr_23:   addi %r12,%r0,-144
              stvx %v23,%r12,%r0
_savevr_24:   addi %r12,%r0,-128
              stvx %v24,%r12,%r0
_savevr_25:   addi %r12,%r0,-112
              stvx %v25,%r12,%r0
_savevr_26:   addi %r12,%r0,-96
              stvx %v26,%r12,%r0
_savevr_27:   addi %r12,%r0,-80
              stvx %v27,%r12,%r0
_savevr_28:   addi %r12,%r0,-64
              stvx %v28,%r12,%r0
_savevr_29:   addi %r12,%r0,-48
              stvx %v29,%r12,%r0
_savevr_30:   addi %r12,%r0,-32
              stvx %v30,%r12,%r0
_savevr_31:   addi %r12,%r0,-16
              stvx %v31,%r12,%r0
              blr

_restvr_20:   addi %r12,%r0,-192
              lvx  %v20,%r12,%r0
_restvr_21:   addi %r12,%r0,-176
              lvx  %v21,%r12,%r0
_restvr_22:   addi %r12,%r0,-160
              lvx  %v22,%r12,%r0
_restvr_23:   addi %r12,%r0,-144
              lvx  %v23,%r12,%r0
_restvr_24:   addi %r12,%r0,-128
              lvx  %v24,%r12,%r0
_restvr_25:   addi %r12,%r0,-112
              lvx  %v25,%r12,%r0
_restvr_26:   addi %r12,%r0,-96
              lvx  %v26,%r12,%r0
_restvr_27:   addi %r12,%r0,-80
              lvx  %v27,%r12,%r0
_restvr_28:   addi %r12,%r0,-64
              lvx  %v28,%r12,%r0
_restvr_29:   addi %r12,%r0,-48
              lvx  %v29,%r12,%r0
_restvr_30:   addi %r12,%r0,-32
              lvx  %v30,%r12,%r0
_restvr_31:   addi %r12,%r0,-16
              lvx  %v31,%r12,%r0
              blr

