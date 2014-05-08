; goal of test
; test store undefined data to a specified position
; make the stored place point to everywhere 

@A = global i32 7

define void @main() {
	%A1 = alloca i32*
	%A2 = inttoptr i32 15 to i32*
	store i32* %A2, i32** %A1
	%A3 = load i32** %A1
	unreachable
}

;Expected Output
;A -> A_VALUE
;A1 -> A1_HEAP
;A2 -> EVERYTHING
;A3 -> EVERYTHING
