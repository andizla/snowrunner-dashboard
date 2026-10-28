; creep_stub.asm: the hook on the game's wheels' drive (gearbox_hooks.cpp, CreepHook; models.h, CreepShare). The drive
; takes the action in rcx, the drive throttle in xmm1 and the step's seconds in xmm2. For the action the slot names (the
; driven truck's), the throttle becomes the larger of its own and the slot's share; every other call passes as it came.
; rax and the flags are all it uses besides: neither carries anything into a function. Then on into the drive, behind
; Detours' jump.

EXTERN g_creepWay:QWORD
EXTERN g_creepSlot:QWORD
EXTERN g_creepHits:DWORD
EXTERN g_creepCalls:DWORD

.code
CreepDrive PROC
    lock inc dword ptr [g_creepCalls]
    mov rax, qword ptr [g_creepSlot]   ; { uint64 action, float share }, or 0 for none
    test rax, rax
    jz pass
    cmp rcx, qword ptr [rax]
    jne pass
    lock inc dword ptr [g_creepHits]
    maxss xmm1, dword ptr [rax + 8]
pass:
    jmp qword ptr [g_creepWay]
CreepDrive ENDP

END
