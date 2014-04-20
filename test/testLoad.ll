@A = global i32 7

define i32 @test2() {
	%A1 = alloca i32*
	store i32* @A, i32** %A1
        %A2 = load i32** %A1
	%A3 = load i32** %A1
	%A4 = load i32* %A3
        ret i32 %A4
}
