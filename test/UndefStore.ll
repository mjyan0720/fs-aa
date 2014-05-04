@A = global i32 7

define void @main() {
	%A1 = inttoptr i32 15 to i32**
	store i32* @A, i32** %A1
	%A2 = load i32** %A1
	unreachable
}

;Expected Output
;A -> A_VALUE
;B -> B_VALUE
;C -> C_VALUE
;A1 -> A1_HEAP
;A2 -> A2_HEAP
;A3 -> A1_HEAP
;A3 -> A2_HEAP
;A4 -> A_VALUE
;A4 -> B_VALUE
;A4 -> C_VALUE
