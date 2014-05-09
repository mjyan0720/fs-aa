; Goal of test
; Test store a varaible to undefined place
; -- this should make every variable
;    top and address-taken ones points to that place
; -- the tricky part is it may affects different functions

@A = global i32 7
@B = global i32 8

define void @main() {
	%A1 = inttoptr i32 15 to i32**	; A1 is points to everywhere
	store i32* @A, i32** %A1	; Everywhere should points to A__VALUE
	
	unreachable
}


define void @func1(){
	%A1 = alloca i32*
	%A2 = load i32** %A1	; A1 & A1_HEAP --> A__VALUE
				; A2 --> A__VALUE
	unreachable
}

define void @func2(){
	%A1 = inttoptr i32 20 to i32**
	store i32* @B, i32** %A1	; everywhere points to B__VALUE
					; this triggers reprocessing all other functions
	unreachable
}


;Expected Output
; A -> A_VALUE
; B -> B_VALUE
; main_A1 -> A_VALUE
; main_A1 -> B_VALUE
; func1_A1 -> func1_A1__HEAP
; func1_A1 -> A_VALUE
; func1_A1 -> B_VALUE
; func1_A2 -> A_VALUE
; func1_A2 -> B_VALUE
; func2_A1 -> A_VALUE
; func2_A1 -> B_VALUE
