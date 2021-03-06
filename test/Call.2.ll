;Goal of this test:
;test case when call function changes addr-taken information
; the modified information should be able to propagate to return call site
; it also does a strong update across functions

@A = global i32 10
@B = global i32 11

define i32 @main() {
	%X = alloca i32*
	store i32* @A, i32** %X
	call i32  @call_1(i32** %X)
	%Z = load i32** %X
	ret i32 0
}

define i32 @call_1(i32** %A) {
	store i32* @B, i32** %A
	ret i32 0
}

;expected output
;A -> A_VALUE
;B -> B_VALUE
;main_X -> X_HEAP
;main_Z -> B_VALUE (not A_VALUE)
