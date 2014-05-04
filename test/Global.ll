@A = global i32      99
@B = global i32*     @A
@C = global i32**    @B
@D = global i32***   @C
@E = global i32****  @D
@F = global i32***** @E

define i32 @main() {
	%E   = load i32****** @F
	%D   = load i32*****  %E
	%C   = load i32****   %D
	%B   = load i32***    %C
	%A   = load i32**     %B
	%ret = load i32*      %A
	ret i32 %ret
}
