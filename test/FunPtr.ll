target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@A = global i32 10

define i32 @main() {
	%A3 = alloca i32*                              ; alloca

	%A7 = alloca i32 (i32**)*                      ; gen function pointer
	store i32 (i32**)* @ptrtest, i32 (i32**)** %A7
	%A8 = load i32 (i32**)** %A7

	%A9 = call i32 %A8(i32** %A3)                  ; test function pointer
	ret i32 0
}

define i32 @ptrtest(i32 **%A1) {
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
	ret i32 0
}

; I have no idea what this file should output
; I have only tested that the code doesn't crash
; not that it works properly
