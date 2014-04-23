@A = global i32 10

define i32 @start() {
	%A3 = alloca i32*
	%A4 = call i32 @call1()                       ; test no arguments
	%A5 = call i32 @call2(i32** %A3)              ; test one argument
	%A6 = call i32 @call3(i32** %A3, i32** %A3)   ; test two arguments

	%A7 = alloca i32 (i32**)*                     ; gen function pointer
	store i32 (i32**)* @call2, i32 (i32**)** %A7
	%A8 = load i32 (i32**)** %A7

	%A9 = call i32 %A8(i32** %A3)                 ; test function pointer
	ret i32 0
}

define i32 @call1() {
	ret i32 0
}

define i32 @call2(i32 **%A1) {
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
	ret i32 0
}

define i32 @call3(i32 **%A1, i32 **%A2) {
	store i32* @A, i32** %A1
	%A3 = load i32** %A1
	ret i32 0
}

; I have no idea what this file should output
; I have only tested that the code doesn't crash
; not that it works properly
