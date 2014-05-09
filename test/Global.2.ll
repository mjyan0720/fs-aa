; goal of test
; test for effect of modify global information



@G0 = global i32 10
@G1 = global i32* @G0
@G2 = global i32** @G1

define i32 @main(){
entry:
	%A1 = alloca i32*
	store i32** %A1, i32*** @G2	; G2->G1->G0 , G1->A1_HEAP
					; this one should be a strong update
	ret i32 0
}
