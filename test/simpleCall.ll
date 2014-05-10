; naive test for call and return

@G = global i32 10

define i32 @start() {
	%S1 = alloca i32*
	store i32* @G, i32** %S1
	%S2 = call i32* @call1(i32** %S1) ; test one argument
	ret i32 0
}

define i32* @call1(i32 **%C1) {
	%C2 = load i32** %C1
	ret i32* %C2
}

; Expected Output:
; G -> G_VALUE               - because of initializer, is this the behavior we want?
; call1_C1 -> start_S1__HEAP    -
; call1_C2 -> G_VALUE
; start_S1 -> start_S1__HEAP
; start_S2 -> G_VALUE 
