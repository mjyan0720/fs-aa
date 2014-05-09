; goal of test
; for uninitialized load, it should points to everywhere


@A = global i32 7

define void @main() {
	%A1 = load i32* @A
	%A2 = add i32 %A1, %A1
	%A3 = inttoptr i32 %A2 to i32*
	%A4 = load i32* %A3
	unreachable
}


define i32 @func1() {
	


}

; Expected Output
; A       -> A_VALUE
; main    -> main_FUNCTION
; main_A1 -> A1_HEAP
; main_A4 -> EVERYTHING
