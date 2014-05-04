@A = global i32 7
@B = global i32 8
@C = global i32 9

define i32 @test2() {
	%A1 = alloca i32*
	br i1 1, label %Ifthen, label %Ifelse
Ifthen:
	store i32* @A, i32** %A1
	br label %end
Ifelse:
	store i32* @B, i32** %A1
	br label %end
end:
	store i32* @C, i32** %A1
	%A2 = load i32** %A1
	ret i32 0
}

;Expected Output
;A -> A_VALUE
;B -> B_VALUE
;C -> C_VALUE
;A1 -> A1_HEAP
;A2 -> C_VALUE
