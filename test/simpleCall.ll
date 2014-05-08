; naive test for call and return

@A = global i32 10

define i32 @start() {
	%A3 = alloca i32*
	store i32* @A, i32** %A3
	%A5 = call i32* @call1(i32** %A3) ; test one argument
	; call i32* @call1(i32** %A3) ; test one argument
	ret i32 0
}

define i32* @call1(i32 **%A1) {
	%A2 = load i32** %A1
	ret i32* %A2
}

; Expected Output:
; A -> A__VALUE
; call1_A1 -> call1_A1__ARGUMENT
; call1_A1 -> start_A3__HEAP
; call1_A2 -> A__VALUE
; start_A3 -> start_A3__HEAP
; start_A5 -> A__VALUE
