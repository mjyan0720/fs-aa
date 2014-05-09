; test complex singlecopy, multi-chains
; one more test for complex initialize of global variables


target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%T1 = type { i32, i32*, i32** }

@P = global i32 100
@Q = global i32 200
@G = global i32* @P
@T = global i32* @Q
@M = global i32 300
@N = global i32* @M
@S = global %T1 { i32 10, i32* @M,  i32** @N}		; S->EVERYTHING

define i32 @main() {
entry:
	%A1 = alloca %T1					; A1 -> A1_HEAP/P
	%A2 = getelementptr %T1* %A1, i32 0, i32 2		; A2 -> A1_HEAP/P
	store i32** @T, i32*** %A2				; A1_HEAP -> Q/P
	br label %LoopBegin
LoopBegin:
	%A4 = phi %T1* [%A1, %entry], [@S, %LoopEnd]		; A4 -> EVERYTHING
	%A3 = getelementptr %T1* %A4, i32 0, i32 2		; A3 -> EVERYTHING
	store i32** @G, i32*** %A3				; EVERYTING -> P
	br label %LoopEnd
LoopEnd:
	br i1 1, label %LoopBegin, label %end
end:
	%A10 = load i32** @N					; A10 -> M_VALUE/P/P__VALUE/
	ret i32 0
}

;Expected Output
;P -> P__VALUE
;Q -> Q__VALUE
;G -> P
;T -> Q
;M -> M__VALUE
;N -> M
;S -> S__VALUE
;S -> M
;S -> N
;main -> main__FUNCTION
;main_A1 -> main_A1__HEAP
;main_A4 -> S__VALUE
;main_A4 -> main_A1__HEAP
;main_A10 -> M__VALUE
;main_A10 -> T__VALUE??
