
define i32 @test2() {
entry:
	%A1 = alloca i32*
	br label %LoopBegin
LoopBegin:
	%A2 = phi i32** [%A1, %entry], [%A3, %LoopEnd]
	%A3 = alloca i32*
	br label %LoopEnd
LoopEnd:
	br i1 1, label %LoopBegin, label %End
End:
        ret i32 0
}

;Expected Output
;A1 -> A1_HEAP
;A3 -> A3_HEAP
;A2 -> A1_HEAP
;A2 -> A3_HEAP
