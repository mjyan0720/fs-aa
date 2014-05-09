; goal of test
; this test is really tricky
; when we store undefined value to undefined place
; this makes everything points to everywhere
; we will return everything alias



define void @main() {
	%A1 = inttoptr i32 15 to i32*
	%A2 = inttoptr i32 69 to i32**
	store i32* %A1, i32** %A2
	%A3 = load i32** %A2
	unreachable
}

;Expected Output
; EVERYTHING -> EVERYTHING
