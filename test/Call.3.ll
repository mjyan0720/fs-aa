; goal of test
; test one function calls the same function multiple times

@A = global i32 7
@B = global i32 9

define void @main() {
	%A0 = alloca i32*
	%A1 = call i32*(i32**)* @func2(i32** %A0)	; A0 hasn't been initialized
							; A1 -> everywhere
	store i32* @B, i32** %A0			; A0 is initialized
	%A3 = call i32*(i32**)* @func2(i32** %A0)	; A3 ->B_VALUE, A3?->A_VALUE
	unreachable
}


define i32* @func2(i32** %A1){
	%A2 = load i32** %A1	; A2 load uninitialized
				; A2 points to everywhere
	store i32* @A, i32** %A1
	ret i32* %A2
}

; Expected Output
; main_A1 -> B_VALUE 
; main_A3 -> B_VALUE
