@A = global i32 7

define i32 @test2() {
	%A1 = alloca i32*
;	%A_Value = load i32* @A
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
        ret i32 0
}
