@A = global i32 7

define void @main() {
	%A1 = alloca i32*
	store i32* @A, i32** %A1
	%A2 = ptrtoint i32** %A1 to i32
	%A3 = inttoptr i32   %A2 to i32**
	%A4 = load i32** %A3
	unreachable
}

; Expected Output
; A       -> A_VALUE
; main    -> main_FUNCTION
; main_A1 -> A1_HEAP
; main_A4 -> A_VALUE
