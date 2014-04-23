target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@G = global i32 3

define i32 @main() {
entry:
	%A1 = load i32* @G
	%A2 = add i32  %A1, 1
	
	%A3 = bitcast i32 %A2 to <4 x i8> 
	%A4 = bitcast <4 x i8> %A3 to i32
	
        ret i32 0
}

;Expected Output
;Value Map
;A1: 1 
;A2: /
;A3: 2 
;A4: 2

