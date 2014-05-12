@G1 = global i32 31
@G2 = global i32 67
@G3 = global i32 95

define i32 @func1(i32* %C1ARG, i32** %C1RET) {
	%C1ALL = alloca i32
	store i32* %C1ALL, i32** %C1RET
	ret i32 0
}

define i32 @func2(i32* %C2ARG, i32** %C2RET) {
	store i32* %C2ARG, i32** %C2RET
	ret i32 0
}

define i32 @main() {
	%FP  = alloca i32 (i32*,i32**)*
	%R1  = alloca i32*
	%R2  = alloca i32*
	%R3  = alloca i32*
	br i1 1, label %if, label %else
if:
	; FP points to func1
	store i32 (i32*, i32**)* @func1, i32 (i32*,i32**)** %FP
	%F1 = load i32 (i32*,i32**)** %FP
	call i32 %F1(i32* @G1, i32** %R1)
	br label %endif
else:
	; FP points to func2
	store i32 (i32*, i32**)* @func2, i32 (i32*,i32**)** %FP
	%F2 = load i32 (i32*,i32**)** %FP
	call i32 %F2(i32* @G2, i32** %R2)
	br label %endif
endif:
	; FP points to func1 & func2
	%F3 = load i32 (i32*,i32**)** %FP
	call i32 %F3(i32* @G3, i32** %R3)
	; Where does R1, R2, R3 point?
	%V1 = load i32** %R1
	%V2 = load i32** %R2
	%V3 = load i32** %R3
	ret i32 0
}
; expected result
; main_V1 -> func1_C1ALL_HEAP
; main_V2 -> G2_VALUE, G3_VALUE
; main_V3 -> G2_VALUE, G3_VALUE, func1_C1ALL_HEAP
