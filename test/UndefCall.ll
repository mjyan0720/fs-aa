@A = global i32 1
@B = global i32 2
@C = global i32 3
@D = global i32* @A  ; these initializer expresssions are currently not handled
@E = global i32* @B
@F = global i32* @C

define i32 @main() {
	%num = add i32 7, 10
	%callee = inttoptr i32 %num to i32* ()*
	%val = call i32* %callee()
	%ret = load i32* %val
	ret i32 %ret
}

define i32* @call1() {
	%ret = load i32** @D
	ret i32* %ret
}

define i32* @call2() {
	%ret = load i32** @E
	ret i32* %ret
}

define i32* @call3() {
	%ret = load i32** @F
	ret i32* %ret
}
