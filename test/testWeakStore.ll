@A = global i32 7
@B = global i32 8
@C = global i32 9

define i32 @test2() {
	%A1 = alloca i32*
	%A2 = alloca i32*
	br i1 1, label %Ifthen, label %Ifelse
Ifthen:
	store i32* @A, i32** %A1
	br label %end
Ifelse:
	store i32* @B, i32** %A2
	br label %end
end:
	%A3 = phi i32** [%A1, %Ifthen], [%A2, %Ifelse]
;	store i32* @C, i32** %A3
	%A4 = load i32** %A3
        ret i32 0
}

;Expected Output
;A -> A_VALUE
;B -> B_VALUE
;C -> C_VALUE
;A1 -> A1_HEAP
;A2 -> A2_HEAP
;A3 -> A1_HEAP
;A3 -> A2_HEAP
;A4 -> A_VALUE
;A4 -> B_VALUE
