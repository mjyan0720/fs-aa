target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%T1 = type { i32, i32*, i32* }
@G = global i32 3

define i32 @main() {
entry:
	%A1 = alloca %T1
	%A2 = getelementptr %T1* %A1, i32 0, i32 1
	br label %LoopBegin
LoopBegin:
	%A4 = phi i32** [%A2, %entry], [%A3, %LoopEnd]
	%A3 = getelementptr i32** %A4, i32 0
	%A5 = getelementptr i32** %A3, i32 0
	br label %LoopEnd
LoopEnd:
	br i1 1, label %LoopBegin, label %end
end:
        ret i32 0
}

;Expected Output
;Value Map
;A1: 5 
;A2: 5
;A3: 7 
;A4: 7

;A1 -> A1_HEAP
;A2 -> A1_HEAP
;A3 -> A1_HEAP
;A3 -> A1_HEAP
