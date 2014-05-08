; goal of test
; test chained singlecopy
; special case, with phi, the instruction result may assign the same id as its operand

target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@T = global i32 1
@G = global i32* @T

define i32 @main() {
entry:
	%A1 = alloca i32*
	%A8 = alloca i32**
	store i32** %A1, i32*** %A8
	%A9 = bitcast i32*** %A8 to i32*
	br label %l1
l1:
	%A2 = phi i32* [%A9, %entry], [%A3, %l2]
	
; here, A2 and A3 are assigned the same id

	%A5 = load i32* %A2
	br label %l2
l2:
	%A4 = bitcast i32* %A2 to i32***
	store i32** @G, i32*** %A4
	%A6 = ptrtoint i32*** %A4 to i32
	%A3 = inttoptr i32 %A6 to i32*
	br label %l1	
        ret i32 0
}

;Expected Output
;T -> T__VALUE
;P -> P__VALUE
;G -> T
;main -> main__FUNCTION
;main_A1 -> main_A1__HEAP
;main_A8 -> main_A8__HEAP
;main_A2 -> main_A8__HEAP
;main_A5 -> T
;main_A5 -> main_A1__HEAP

