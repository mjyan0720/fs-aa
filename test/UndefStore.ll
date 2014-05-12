; Goal of test
; Test store a varaible to undefined place
; -- this should make every variable
;    top and address-taken ones points to that place
; -- the tricky part is it may affects different functions

@A = global i32 7
@B = global i32 8

define void @main() {
	%A1 = inttoptr i32 15 to i32**	; A1 is points to everywhere
	%A2 = alloca i32*
	store i32* @A, i32** %A1	; Everywhere should points to A__VALUE
	call i32()* @func2()
	%A3 = load i32** %A2
	unreachable
}

define i32 @func2(){
	%A1 = inttoptr i32 20 to i32**
	store i32* @B, i32** %A1	; everywhere points to B__VALUE
					; this triggers reprocessing all other functions
	ret i32 0
}


;Expected Output
; main_A3 -> A_VALUE, B_VALUE 
