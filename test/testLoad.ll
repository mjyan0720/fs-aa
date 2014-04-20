define i32 @test2() {
	%A1 = alloca i32*
        %A2 = load i32** %A1
	%A3 = load i32* %A2
        return i32 %A3
}
