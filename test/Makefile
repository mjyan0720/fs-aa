
%.bc: %.c
	clang -emit-llvm -c  $< -o $@

%.aa.bc: %.bc
	opt -load ../../../../Debug+Asserts/lib/FlowSensitiveAliasAnalysis.so -load ../../../../Debug+Asserts/lib/checker.so -fs-aa -check-aa  $< -o $@ -debug-only=flowsensitive-aa

%.ll: %.bc
	llvm-dis $< -o $@

%.check.bc: %.aa.bc
	llvm-link $< jf_checker_map.bc jf_log.bc -o $@
