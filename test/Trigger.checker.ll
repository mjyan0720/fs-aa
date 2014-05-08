; ModuleID = 'Trigger.checker.bc'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [2 x i8] c"0\00", align 1
@.str1 = private unnamed_addr constant [18 x i8] c"Trigger.checker.c\00", align 1
@__PRETTY_FUNCTION__.foo = private unnamed_addr constant [33 x i8] c"int foo(int, int, int, int, int)\00", align 1

; Function Attrs: nounwind uwtable
define i32 @foo(i32 %x, i32 %x1, i32 %x2, i32 %x3, i32 %x4) #0 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  %array = alloca i32**, align 8
  %ptr = alloca i32**, align 8
  %temp = alloca i32*, align 8
  store i32 %x, i32* %1, align 4
  store i32 %x1, i32* %2, align 4
  store i32 %x2, i32* %3, align 4
  store i32 %x3, i32* %4, align 4
  store i32 %x4, i32* %5, align 4
  %6 = call noalias i8* @malloc(i64 32) #3
  %7 = bitcast i8* %6 to i32**
  store i32** %7, i32*** %array, align 8
  %8 = load i32*** %array, align 8
  %9 = getelementptr inbounds i32** %8, i64 0
  store i32* %2, i32** %9, align 8
  %10 = load i32*** %array, align 8
  %11 = load i32* %1, align 4
  %12 = sext i32 %11 to i64
  %13 = getelementptr inbounds i32** %10, i64 %12
  store i32** %13, i32*** %ptr, align 8
  %14 = load i32*** %ptr, align 8
  %15 = load i32** %14, align 8
  store i32* %15, i32** %temp, align 8
  %16 = load i32** %temp, align 8
  %17 = load i32* %16, align 4
  %18 = icmp sgt i32 %17, 0
  br i1 %18, label %19, label %20

; <label>:19                                      ; preds = %0
  br label %21

; <label>:20                                      ; preds = %0
  call void @__assert_fail(i8* getelementptr inbounds ([2 x i8]* @.str, i32 0, i32 0), i8* getelementptr inbounds ([18 x i8]* @.str1, i32 0, i32 0), i32 21, i8* getelementptr inbounds ([33 x i8]* @__PRETTY_FUNCTION__.foo, i32 0, i32 0)) #4
  unreachable

; <label>:21                                      ; preds = %19
  %22 = load i32** %temp, align 8
  %23 = load i32* %22, align 4
  ret i32 %23
}

; Function Attrs: nounwind
declare noalias i8* @malloc(i64) #1

; Function Attrs: noreturn nounwind
declare void @__assert_fail(i8*, i8*, i32, i8*) #2

; Function Attrs: nounwind uwtable
define i32 @main() #0 {
  %1 = alloca i32, align 4
  %x = alloca i32, align 4
  store i32 0, i32* %1
  store i32 0, i32* %x, align 4
  %2 = load i32* %x, align 4
  %3 = call i32 @foo(i32 %2, i32 10, i32 20, i32 30, i32 40)
  ret i32 %3
}

attributes #0 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { noreturn nounwind "less-precise-fpmad"="false" "no-frame-pointer-elim"="true" "no-frame-pointer-elim-non-leaf" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "stack-protector-buffer-size"="8" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #3 = { nounwind }
attributes #4 = { noreturn nounwind }

!llvm.ident = !{!0}

!0 = metadata !{metadata !"clang version 3.4 (tags/RELEASE_34/final)"}
