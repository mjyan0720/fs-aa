target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @main() {
	%A1 = alloca i32*
	%A2 = alloca i32*
	br i1 1, label %Ifthen, label %Ifelse
Ifthen:
	br label %end
Ifelse:
	br label %end
end:
	%A3 = phi i32** [%A1, %Ifthen], [%A2, %Ifelse]
        ret i32 0
}

;Expected Output
;A1 -> A1_HEAP
;A2 -> A2_HEAP
;A3 -> A1_HEAP
;A3 -> A2_HEAP
