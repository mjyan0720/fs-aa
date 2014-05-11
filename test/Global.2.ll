; goal of this test
; test complicated initialize global variables

%struct = type {i32*, i32}

@A = global i32      99
@B = global %struct{i32* @A, i32 10}		; we can't handle this initializer
@C = global i32**   getelementptr (%struct* @B, i32 0, i32 0)

@M = global i32  100
@N = global i8* bitcast (i32* @M to i8*)
@P = global i32**   bitcast (i8** @N to i32**)
@Q = global i32***  getelementptr (i32*** @P, i32 0)

define i32 @main() {
	%C   = load i32***   @C 
	%B   = load i32**    %C
	%A   = load i32*     %B

	%Q  = load i32**** @Q
	%P  = load i32***  %Q
	%N  = load i32**   %P
	%M  = load i32*    %N
	ret i32 0
}


; expected result
;main_C -> B__VALUE
;main_B -> EVERYTHING
;main_A -> EVERYTHING
;main_Q -> P__VALUE
;main_P -> N__VALUE
;main_N -> M__VALUE
;main_M -> EVERYTHING


