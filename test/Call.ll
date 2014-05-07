; Goal of this test
; test different arguments passing to called functions
; test general case of indirect call

@A = global i32 10

define i32 @start() {
	%A1 = alloca i32*
	%A2 = call i32* @call1()                       ; test no arguments
	%A3 = call i32 @call2(i32** %A1)              ; test one argument
	%A4 = call i32 @call3(i32** %A1, i32* %A2)   ; test two arguments

	%A5 = alloca i32 (i32**)*                     ; gen function pointer
	store i32 (i32**)* @call2, i32 (i32**)** %A5
	%A6 = load i32 (i32**)** %A5

	%A7 = call i32 %A6(i32** %A1)                 ; test function pointer
	ret i32 0
}

define i32* @call1() {
	%A1 = alloca i32
	ret i32* %A1
}

define i32 @call2(i32 **%A1) {
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
	ret i32 0
}

define i32 @call3(i32 **%A1, i32 *%A2) {
	store i32* @A, i32** %A1
	%A3 = load i32* %A2
	ret i32 %A3
}

; Expected Result
; A --> A_Value
; start_A1 --> start_A1_HEAP
; call1_A1 --> call1_A1_HEAP
; start_A2 --> call1_A1_HEAP
; call2_A1 --> start_A1_HEAP
; call2_A2 --> A_Value
; call3_A1 --> start_A1_HEAP
; call3_A2 --> call1_A1_HEAP
; call3_A3 --> EVERYTHING
