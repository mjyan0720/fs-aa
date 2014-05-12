; Goal of test
; test for indirect call
; function pointer contains multiple functions
; some type matche, some don't


target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@A = global i32 10
@B = global i32 11
@C = global i32 12
@D = global i8  13

%funptr = type {i32 (i32**)*, i32 (i8**)*, i8 (i32**)*}

define i32 @main() {
	%A1 = alloca i32*                              ; alloca

	%A2 = alloca %funptr	                         ; gen function pointer
	%A3 = getelementptr %funptr* %A2, i32 0, i32 0
	switch i32 3, label %l1 [i32 0, label %l2
														i32 1, label %l3
														i32 2, label %l4]
	br i1 1, label %l1, label %l2
l1:
	store i32 (i32**)* @func1, i32 (i32**)** %A3
	br label %l5
l2:
	store i32 (i32**)* @func2, i32 (i32**)** %A3
	br label %l5
l3:
	%A4 = getelementptr %funptr* %A2, i32 0, i32 2
	store i8 (i32**)* @func3, i8 (i32**)** %A4
	br label %l5
l4:
	%A5 = getelementptr %funptr* %A2, i32 0, i32 1
	store i32 (i8**)* @func4, i32 (i8**)** %A5
	br label %l5
l5:
	%A6 = getelementptr %funptr* %A2, i32 0, i32 0	; Now A2 contains all function pointers
	%A8 = load i32 (i32**)** %A6

	%A9 = call i32 %A8(i32** %A1)                  ; this call should only call func1 & func2
	%A10 = load i32** %A1				; only func1 and func2 has impact
	ret i32 0
}

define i32 @func1(i32 **%A1) {
	store i32* @A, i32** %A1
	ret i32 0
}

define i32 @func2(i32 **%A1) {
	store i32* @B, i32** %A1
	ret i32 0
}

define i8 @func3(i32 **%A1) {
	store i32* @C, i32** %A1
	ret i8 0
}

define i32 @func4(i8 **%A1) {
	store i8* @D, i8** %A1
	ret i32 0
}


; expected Result
; Only need to check A10
; A8 -> func4_FUNCTION, func3_FUNCTION, func2_FUNCTION, func1_FUNCTION
; A10 --> A_VALUE
; A10 --> B_VALUE
