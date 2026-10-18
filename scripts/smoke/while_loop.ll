; ModuleID = 'scripts/smoke/while_loop.hv_module'
source_filename = "scripts/smoke/while_loop.hv_module"

%JITStackFrame = type { ptr, [32 x i64], [32 x i64], i32, [32 x i32], [32 x i32], [32 x i32], i32 }

@0 = private constant [9 x i8] c"__main__\00"

define i64 @__main__(ptr %0, ptr %1, i32 %2) {
entry:
  %gc_frame = alloca %JITStackFrame, align 8
  %3 = getelementptr inbounds i64, ptr %1, i32 0
  call void @havel_gc_register_roots(ptr %0, ptr %gc_frame, ptr %3, i32 1)
  %l0 = alloca i64, align 8
  store i64 9221964661971222528, ptr %l0, align 4
  br label %ip0

ip0:                                              ; preds = %entry
  br label %ip1

ip1:                                              ; preds = %ip0
  br label %ip2

ip2:                                              ; preds = %ip1
  call void @havel_vm_global_set(ptr %0, i32 0, i64 9221401712017801216)
  br label %ip3

ip3:                                              ; preds = %ip2
  br label %ip4

ip4:                                              ; preds = %ip10, %ip3
  %4 = call i64 @havel_vm_global_get(ptr %0, i32 0)
  br label %ip5

ip5:                                              ; preds = %ip4
  br label %ip6

ip6:                                              ; preds = %ip5
  %5 = call i64 @havel_vm_lt(i64 %4, i64 9221401712017801217)
  br label %ip7

ip7:                                              ; preds = %ip6
  %6 = call i32 @havel_vm_is_truthy(i64 %5)
  %7 = icmp eq i32 %6, 0
  br i1 %7, label %ip11, label %ip8

ip8:                                              ; preds = %ip7
  br label %ip9

ip9:                                              ; preds = %ip8
  %8 = call i32 @havel_vm_locals_base(ptr %0)
  call void @havel_vm_close_upvalues(ptr %0, i32 %8)
  call void @havel_gc_unregister_roots(ptr %gc_frame)
  ret i64 9221401712017801222

ip10:                                             ; No predecessors!
  call void @havel_vm_backedge(ptr %0, i32 10)
  br label %ip4

ip11:                                             ; preds = %ip7
  br label %ip12

ip12:                                             ; preds = %ip11
  br label %ip13

ip13:                                             ; preds = %ip12
  call void @havel_vm_global_set(ptr %0, i32 1, i64 9221401712017801216)
  br label %ip14

ip14:                                             ; preds = %ip13
  store i64 9221401712017801216, ptr %l0, align 4
  br label %ip15

ip15:                                             ; preds = %ip14
  %9 = call i64 @havel_vm_global_get(ptr %0, i32 1)
  br label %ip16

ip16:                                             ; preds = %ip15
  br label %ip17

ip17:                                             ; preds = %ip16
  %10 = call i64 @havel_vm_neq(i64 %9, i64 9221401712017801222)
  br label %ip18

ip18:                                             ; preds = %ip17
  %11 = call i32 @havel_vm_is_truthy(i64 %10)
  %12 = icmp eq i32 %11, 0
  br i1 %12, label %ip31, label %ip19

ip19:                                             ; preds = %ip18
  %13 = call i64 @havel_vm_global_get(ptr %0, i32 2)
  br label %ip20

ip20:                                             ; preds = %ip19
  br label %ip21

ip21:                                             ; preds = %ip20
  %14 = call i64 @havel_vm_global_get(ptr %0, i32 4)
  br label %ip22

ip22:                                             ; preds = %ip21
  %15 = call i64 @havel_vm_global_get(ptr %0, i32 1)
  br label %ip23

ip23:                                             ; preds = %ip22
  %call_args = alloca [2 x i64], align 8
  %16 = getelementptr inbounds [2 x i64], ptr %call_args, i32 0, i32 1
  store i64 %15, ptr %16, align 4
  %17 = getelementptr inbounds [2 x i64], ptr %call_args, i32 0, i32 0
  store i64 %14, ptr %17, align 4
  %18 = getelementptr inbounds [2 x i64], ptr %call_args, i32 0, i32 0
  %19 = call i64 @havel_vm_call(ptr %0, ptr %18, i32 2)
  br label %ip24

ip24:                                             ; preds = %ip23
  %20 = and i64 %19, 1970324836974592
  %21 = icmp eq i64 %20, 281474976710656
  %22 = and i1 false, %21
  br i1 %22, label %op24_int, label %op24_chk_dbl

ip25:                                             ; preds = %op24_merge
  %call_args1 = alloca [2 x i64], align 8
  %23 = getelementptr inbounds [2 x i64], ptr %call_args1, i32 0, i32 1
  store i64 %46, ptr %23, align 4
  %24 = getelementptr inbounds [2 x i64], ptr %call_args1, i32 0, i32 0
  store i64 %13, ptr %24, align 4
  %25 = getelementptr inbounds [2 x i64], ptr %call_args1, i32 0, i32 0
  %26 = call i64 @havel_vm_call(ptr %0, ptr %25, i32 2)
  br label %ip26

ip26:                                             ; preds = %ip25
  br label %ip27

ip27:                                             ; preds = %ip26
  %27 = call i64 @havel_vm_global_get(ptr %0, i32 5)
  br label %ip28

ip28:                                             ; preds = %ip27
  br label %ip29

ip29:                                             ; preds = %ip28
  %method_args = alloca [1 x i64], align 8
  %28 = getelementptr inbounds [1 x i64], ptr %method_args, i32 0, i32 0
  store i64 9221401712017801471, ptr %28, align 4
  %29 = getelementptr inbounds [1 x i64], ptr %method_args, i32 0, i32 0
  %30 = call i64 @havel_vm_call_method(ptr %0, i64 %27, i32 6, ptr %29, i32 1)
  br label %ip30

ip30:                                             ; preds = %ip29
  br label %ip31

ip31:                                             ; preds = %ip30, %ip18
  %31 = call i64 @havel_vm_global_get(ptr %0, i32 1)
  br label %ip32

ip32:                                             ; preds = %ip31
  %32 = call i32 @havel_vm_locals_base(ptr %0)
  call void @havel_vm_close_upvalues(ptr %0, i32 %32)
  call void @havel_gc_unregister_roots(ptr %gc_frame)
  ret i64 %31

ip33:                                             ; No predecessors!
  br label %ip34

ip34:                                             ; preds = %ip33
  %33 = call i32 @havel_vm_locals_base(ptr %0)
  call void @havel_vm_close_upvalues(ptr %0, i32 %33)
  call void @havel_gc_unregister_roots(ptr %gc_frame)
  ret i64 9221964661971222528

ip35:                                             ; No predecessors!
  call void @havel_gc_unregister_roots(ptr %gc_frame)
  ret i64 9221964661971222528

op24_int:                                         ; preds = %ip24
  %34 = and i64 %19, 281474976710655
  %35 = shl i64 %34, 16
  %36 = ashr i64 %35, 16
  %37 = add i64 105553116266499, %36
  %38 = and i64 %37, 281474976710655
  %39 = or i64 %38, 9221401712017801216
  br label %op24_merge

op24_chk_dbl:                                     ; preds = %ip24
  %40 = and i64 %19, 9221120237041090560
  %41 = icmp ne i64 %40, 9221120237041090560
  %42 = and i1 false, %41
  br i1 %42, label %op24_dbl, label %op24_deopt

op24_dbl:                                         ; preds = %op24_chk_dbl
  %43 = bitcast i64 %19 to double
  %44 = fadd double 0x7FFF600000000003, %43
  %45 = bitcast double %44 to i64
  br label %op24_merge

op24_deopt:                                       ; preds = %op24_chk_dbl
  call void @havel_deoptimize(ptr %0, i64 9223196114994331651, i64 %19, ptr @0)
  br label %op24_merge

op24_merge:                                       ; preds = %op24_deopt, %op24_dbl, %op24_int
  %46 = phi i64 [ %39, %op24_int ], [ %45, %op24_dbl ], [ 9221964661971222528, %op24_deopt ]
  br label %ip25
}

declare void @havel_gc_register_roots(ptr, ptr, ptr, i32)

declare void @havel_vm_global_set(ptr, i32, i64)

declare i64 @havel_vm_global_get(ptr, i32)

declare i64 @havel_vm_lt(i64, i64)

declare i32 @havel_vm_is_truthy(i64)

declare void @havel_vm_close_upvalues(ptr, i32)

declare i32 @havel_vm_locals_base(ptr)

declare void @havel_gc_unregister_roots(ptr)

declare void @havel_vm_backedge(ptr, i32)

declare i64 @havel_vm_neq(i64, i64)

declare i64 @havel_vm_call(ptr, ptr, i32)

declare void @havel_deoptimize(ptr, i64, i64, ptr)

declare i64 @havel_vm_call_method(ptr, i64, i32, ptr, i32)
