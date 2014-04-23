@A = global i32 10

define i32 @start() {
	%A3 = alloca i32*
	store i32* @A, i32** %A3
	%A5 = call i32* @call1(i32** %A3) ; test one argument
	ret i32 0
}

define i32* @call1(i32 **%A1) {
	%A2 = load i32** %A1
	ret i32* %A2
}
