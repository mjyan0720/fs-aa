; goal of test
; test whether load works


@A = global i32 7

define i32 @test2() {
	%A1 = alloca i32*
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
	ret i32 0
}

;Expected Output
;A1 -> A1_HEAP
;A  -> A_VALUE
;A2 -> A_VALUE

;store makes P(A1)=A1_HEAP -> P(A)=A_VALUE 
;load makes  A2 -> P_k(P(A1))=A_VALUE
