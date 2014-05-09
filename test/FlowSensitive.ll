; Goal of test
; test whether it gives flowsensitive result
; finally A1_HEAP points to both A_VALUE and B_VALUE
; but A_then and A_else shouldn't share its result

@A = global i32 7
@B = global i32 8

define i32 @test2() {
	%A1 = alloca i32*
	br i1 1, label %Ifthen, label %Ifelse
Ifthen:
	store i32* @A, i32** %A1
	%A_then = load i32** %A1
	br label %end
Ifelse:
	store i32* @B, i32** %A1
	%A_else = load i32** %A1
	br label %end
end:
;	%A2 = load i32** %A1
        ret i32 0
}

;Expected Output
;A -> A_VALUE
;B -> B_VALUE
;A1 -> A1_HEAP
;A_then -> A_VALUE
;A_else -> B_VALUE

