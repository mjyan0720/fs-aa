; goal of test
; for uninitialized load, it should points to everywhere
; and the effect should be propagatable

@A = global i32 7

define void @main() {
	%A0 = alloca i32*
	%A1 = call i32*(i32**)* @func2(i32** %A0)	; A0 hasn't been initialized, so A1 -> everywhere
	store i32* %A1, i32** %A0
	unreachable
}

define i32* @func2(i32** %A1){
	%A2 = load i32** %A1	; A2 load uninitialized
				; A2 points to everywhere
	ret i32* %A2
}

; Expected Output
; main_A1 -> B_VALUE 
; main_A3 -> B_VALUE
