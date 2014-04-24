OPT_CHECK = opt -load ../../../../Debug+Asserts/lib/FlowSensitiveAliasAnalysis.so -load ../../../../Debug+Asserts/lib/checker.so -fs-aa -check-aa  -debug-only=flowsensitive-aa

OPT = opt -load ../../../../Debug+Asserts/lib/FlowSensitiveAliasAnalysis.so -fs-aa -debug-only=flowsensitive-aa


%.bc: %.c
	clang -emit-llvm -c  $< -o $@

%.aa.bc: %.bc
	$(OPT_CHECK)  $< -o $@ 

%.bc: %.ll
	$(OPT) $< -o $@

%.ll: %.bc
	llvm-dis $< -o $@

%.check.bc: %.aa.bc
	llvm-link $< jf_checker_map.bc jf_log.bc -o $@

clean:
	rm *.aa.ll *.aa.bc *.check.ll *.check.bc
