// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"
#include "codegen-inl.h"
#include "macro-assembler.h"

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm, CFunctionId id) {
  // TODO(428): Don't pass the function in a static variable.
  ExternalReference passed = ExternalReference::builtin_passed_function();
  __ movq(kScratchRegister, passed.address(), RelocInfo::EXTERNAL_REFERENCE);
  __ movq(Operand(kScratchRegister, 0), rdi);

  // The actual argument count has already been loaded into register
  // rax, but JumpToRuntime expects rax to contain the number of
  // arguments including the receiver.
  __ incq(rax);
  __ JumpToRuntime(ExternalReference(id), 1);
}


static void EnterArgumentsAdaptorFrame(MacroAssembler* masm) {
  __ push(rbp);
  __ movq(rbp, rsp);

  // Store the arguments adaptor context sentinel.
  __ push(Immediate(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));

  // Push the function on the stack.
  __ push(rdi);

  // Preserve the number of arguments on the stack. Must preserve both
  // rax and rbx because these registers are used when copying the
  // arguments and the receiver.
  __ Integer32ToSmi(rcx, rax);
  __ push(rcx);
}


static void LeaveArgumentsAdaptorFrame(MacroAssembler* masm) {
  // Retrieve the number of arguments from the stack. Number is a Smi.
  __ movq(rbx, Operand(rbp, ArgumentsAdaptorFrameConstants::kLengthOffset));

  // Leave the frame.
  __ movq(rsp, rbp);
  __ pop(rbp);

  // Remove caller arguments from the stack.
  // rbx holds a Smi, so we convery to dword offset by multiplying by 4.
  // TODO(smi): Find a way to abstract indexing by a smi.
  ASSERT_EQ(kSmiTagSize, 1 && kSmiTag == 0);
  ASSERT_EQ(kPointerSize, (1 << kSmiTagSize) * 4);
  // TODO(smi): Find way to abstract indexing by a smi.
  __ pop(rcx);
  // 1 * kPointerSize is offset of receiver.
  __ lea(rsp, Operand(rsp, rbx, times_half_pointer_size, 1 * kPointerSize));
  __ push(rcx);
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : actual number of arguments
  //  -- rbx : expected number of arguments
  //  -- rdx : code entry to call
  // -----------------------------------

  Label invoke, dont_adapt_arguments;
  __ IncrementCounter(&Counters::arguments_adaptors, 1);

  Label enough, too_few;
  __ cmpq(rax, rbx);
  __ j(less, &too_few);
  __ cmpq(rbx, Immediate(SharedFunctionInfo::kDontAdaptArgumentsSentinel));
  __ j(equal, &dont_adapt_arguments);

  {  // Enough parameters: Actual >= expected.
    __ bind(&enough);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all expected arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(rax, Operand(rbp, rax, times_pointer_size, offset));
    __ movq(rcx, Immediate(-1));  // account for receiver

    Label copy;
    __ bind(&copy);
    __ incq(rcx);
    __ push(Operand(rax, 0));
    __ subq(rax, Immediate(kPointerSize));
    __ cmpq(rcx, rbx);
    __ j(less, &copy);
    __ jmp(&invoke);
  }

  {  // Too few parameters: Actual < expected.
    __ bind(&too_few);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all actual arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(rdi, Operand(rbp, rax, times_pointer_size, offset));
    __ movq(rcx, Immediate(-1));  // account for receiver

    Label copy;
    __ bind(&copy);
    __ incq(rcx);
    __ push(Operand(rdi, 0));
    __ subq(rdi, Immediate(kPointerSize));
    __ cmpq(rcx, rax);
    __ j(less, &copy);

    // Fill remaining expected arguments with undefined values.
    Label fill;
    __ LoadRoot(kScratchRegister, Heap::kUndefinedValueRootIndex);
    __ bind(&fill);
    __ incq(rcx);
    __ push(kScratchRegister);
    __ cmpq(rcx, rbx);
    __ j(less, &fill);

    // Restore function pointer.
    __ movq(rdi, Operand(rbp, JavaScriptFrameConstants::kFunctionOffset));
  }

  // Call the entry point.
  __ bind(&invoke);
  __ call(rdx);

  // Leave frame and return.
  LeaveArgumentsAdaptorFrame(masm);
  __ ret(0);

  // -------------------------------------------
  // Dont adapt arguments.
  // -------------------------------------------
  __ bind(&dont_adapt_arguments);
  __ jmp(rdx);
}


void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  // Stack Layout:
  // rsp: return address
  //  +1: Argument n
  //  +2: Argument n-1
  //  ...
  //  +n: Argument 1 = receiver
  //  +n+1: Argument 0 = function to call
  //
  // rax contains the number of arguments, n, not counting the function.
  //
  // 1. Make sure we have at least one argument.
  { Label done;
    __ testq(rax, rax);
    __ j(not_zero, &done);
    __ pop(rbx);
    __ Push(Factory::undefined_value());
    __ push(rbx);
    __ incq(rax);
    __ bind(&done);
  }

  // 2. Get the function to call from the stack.
  { Label done, non_function, function;
    // The function to call is at position n+1 on the stack.
    __ movq(rdi, Operand(rsp, rax, times_pointer_size, +1 * kPointerSize));
    __ JumpIfSmi(rdi, &non_function);
    __ CmpObjectType(rdi, JS_FUNCTION_TYPE, rcx);
    __ j(equal, &function);

    // Non-function called: Clear the function to force exception.
    __ bind(&non_function);
    __ xor_(rdi, rdi);
    __ jmp(&done);

    // Function called: Change context eagerly to get the right global object.
    __ bind(&function);
    __ movq(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

    __ bind(&done);
  }

  // 3. Make sure first argument is an object; convert if necessary.
  { Label call_to_object, use_global_receiver, patch_receiver, done;
    __ movq(rbx, Operand(rsp, rax, times_pointer_size, 0));

    __ JumpIfSmi(rbx, &call_to_object);

    __ CompareRoot(rbx, Heap::kNullValueRootIndex);
    __ j(equal, &use_global_receiver);
    __ CompareRoot(rbx, Heap::kUndefinedValueRootIndex);
    __ j(equal, &use_global_receiver);

    __ CmpObjectType(rbx, FIRST_JS_OBJECT_TYPE, rcx);
    __ j(below, &call_to_object);
    __ CmpInstanceType(rcx, LAST_JS_OBJECT_TYPE);
    __ j(below_equal, &done);

    __ bind(&call_to_object);
    __ EnterInternalFrame();  // preserves rax, rbx, rdi

    // Store the arguments count on the stack (smi tagged).
    __ Integer32ToSmi(rax, rax);
    __ push(rax);

    __ push(rdi);  // save edi across the call
    __ push(rbx);
    __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
    __ movq(rbx, rax);
    __ pop(rdi);  // restore edi after the call

    // Get the arguments count and untag it.
    __ pop(rax);
    __ SmiToInteger32(rax, rax);

    __ LeaveInternalFrame();
    __ jmp(&patch_receiver);

    // Use the global receiver object from the called function as the receiver.
    __ bind(&use_global_receiver);
    const int kGlobalIndex =
        Context::kHeaderSize + Context::GLOBAL_INDEX * kPointerSize;
    __ movq(rbx, FieldOperand(rsi, kGlobalIndex));
    __ movq(rbx, FieldOperand(rbx, GlobalObject::kGlobalReceiverOffset));

    __ bind(&patch_receiver);
    __ movq(Operand(rsp, rax, times_pointer_size, 0), rbx);

    __ bind(&done);
  }

  // 4. Shift stuff one slot down the stack.
  { Label loop;
    __ lea(rcx, Operand(rax, +1));  // +1 ~ copy receiver too
    __ bind(&loop);
    __ movq(rbx, Operand(rsp, rcx, times_pointer_size, 0));
    __ movq(Operand(rsp, rcx, times_pointer_size, 1 * kPointerSize), rbx);
    __ decq(rcx);
    __ j(not_zero, &loop);
  }

  // 5. Remove TOS (copy of last arguments), but keep return address.
  __ pop(rbx);
  __ pop(rcx);
  __ push(rbx);
  __ decq(rax);

  // 6. Check that function really was a function and get the code to
  //    call from the function and check that the number of expected
  //    arguments matches what we're providing.
  { Label invoke, trampoline;
    __ testq(rdi, rdi);
    __ j(not_zero, &invoke);
    __ xor_(rbx, rbx);
    __ GetBuiltinEntry(rdx, Builtins::CALL_NON_FUNCTION);
    __ bind(&trampoline);
    __ Jump(Handle<Code>(builtin(ArgumentsAdaptorTrampoline)),
            RelocInfo::CODE_TARGET);

    __ bind(&invoke);
    __ movq(rdx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
    __ movsxlq(rbx,
           FieldOperand(rdx, SharedFunctionInfo::kFormalParameterCountOffset));
    __ movq(rdx, FieldOperand(rdx, SharedFunctionInfo::kCodeOffset));
    __ lea(rdx, FieldOperand(rdx, Code::kHeaderSize));
    __ cmpq(rax, rbx);
    __ j(not_equal, &trampoline);
  }

  // 7. Jump (tail-call) to the code in register edx without checking arguments.
  ParameterCount expected(0);
  __ InvokeCode(rdx, expected, expected, JUMP_FUNCTION);
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  // Stack at entry:
  //    rsp: return address
  //  rsp+8: arguments
  // rsp+16: receiver ("this")
  // rsp+24: function
  __ EnterInternalFrame();
  // Stack frame:
  //    rbp: Old base pointer
  // rbp[1]: return address
  // rbp[2]: function arguments
  // rbp[3]: receiver
  // rbp[4]: function
  static const int kArgumentsOffset = 2 * kPointerSize;
  static const int kReceiverOffset = 3 * kPointerSize;
  static const int kFunctionOffset = 4 * kPointerSize;
  __ push(Operand(rbp, kFunctionOffset));
  __ push(Operand(rbp, kArgumentsOffset));
  __ InvokeBuiltin(Builtins::APPLY_PREPARE, CALL_FUNCTION);

  if (FLAG_check_stack) {
    // We need to catch preemptions right here, otherwise an unlucky preemption
    // could show up as a failed apply.
    Label retry_preemption;
    Label no_preemption;
    __ bind(&retry_preemption);
    ExternalReference stack_guard_limit =
        ExternalReference::address_of_stack_guard_limit();
    __ movq(kScratchRegister, stack_guard_limit);
    __ movq(rcx, rsp);
    __ subq(rcx, Operand(kScratchRegister, 0));
    // rcx contains the difference between the stack limit and the stack top.
    // We use it below to check that there is enough room for the arguments.
    __ j(above, &no_preemption);

    // Preemption!
    // Because runtime functions always remove the receiver from the stack, we
    // have to fake one to avoid underflowing the stack.
    __ push(rax);
    __ push(Immediate(Smi::FromInt(0)));

    // Do call to runtime routine.
    __ CallRuntime(Runtime::kStackGuard, 1);
    __ pop(rax);
    __ jmp(&retry_preemption);

    __ bind(&no_preemption);

    Label okay;
    // Make rdx the space we need for the array when it is unrolled onto the
    // stack.
    __ PositiveSmiTimesPowerOfTwoToInteger64(rdx, rax, kPointerSizeLog2);
    __ cmpq(rcx, rdx);
    __ j(greater, &okay);

    // Too bad: Out of stack space.
    __ push(Operand(rbp, kFunctionOffset));
    __ push(rax);
    __ InvokeBuiltin(Builtins::APPLY_OVERFLOW, CALL_FUNCTION);
    __ bind(&okay);
  }

  // Push current index and limit.
  const int kLimitOffset =
      StandardFrameConstants::kExpressionsOffset - 1 * kPointerSize;
  const int kIndexOffset = kLimitOffset - 1 * kPointerSize;
  __ push(rax);  // limit
  __ push(Immediate(0));  // index

  // Change context eagerly to get the right global object if
  // necessary.
  __ movq(rdi, Operand(rbp, kFunctionOffset));
  __ movq(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

  // Compute the receiver.
  Label call_to_object, use_global_receiver, push_receiver;
  __ movq(rbx, Operand(rbp, kReceiverOffset));
  __ JumpIfSmi(rbx, &call_to_object);
  __ CompareRoot(rbx, Heap::kNullValueRootIndex);
  __ j(equal, &use_global_receiver);
  __ CompareRoot(rbx, Heap::kUndefinedValueRootIndex);
  __ j(equal, &use_global_receiver);

  // If given receiver is already a JavaScript object then there's no
  // reason for converting it.
  __ CmpObjectType(rbx, FIRST_JS_OBJECT_TYPE, rcx);
  __ j(below, &call_to_object);
  __ CmpInstanceType(rcx, LAST_JS_OBJECT_TYPE);
  __ j(below_equal, &push_receiver);

  // Convert the receiver to an object.
  __ bind(&call_to_object);
  __ push(rbx);
  __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
  __ movq(rbx, rax);
  __ jmp(&push_receiver);

  // Use the current global receiver object as the receiver.
  __ bind(&use_global_receiver);
  const int kGlobalOffset =
      Context::kHeaderSize + Context::GLOBAL_INDEX * kPointerSize;
  __ movq(rbx, FieldOperand(rsi, kGlobalOffset));
  __ movq(rbx, FieldOperand(rbx, GlobalObject::kGlobalReceiverOffset));

  // Push the receiver.
  __ bind(&push_receiver);
  __ push(rbx);

  // Copy all arguments from the array to the stack.
  Label entry, loop;
  __ movq(rax, Operand(rbp, kIndexOffset));
  __ jmp(&entry);
  __ bind(&loop);
  __ movq(rcx, Operand(rbp, kArgumentsOffset));  // load arguments
  __ push(rcx);
  __ push(rax);

  // Use inline caching to speed up access to arguments.
  Handle<Code> ic(Builtins::builtin(Builtins::KeyedLoadIC_Initialize));
  __ Call(ic, RelocInfo::CODE_TARGET);
  // It is important that we do not have a test instruction after the
  // call.  A test instruction after the call is used to indicate that
  // we have generated an inline version of the keyed load.  In this
  // case, we know that we are not generating a test instruction next.

  // Remove IC arguments from the stack and push the nth argument.
  __ addq(rsp, Immediate(2 * kPointerSize));
  __ push(rax);

  // Update the index on the stack and in register rax.
  __ movq(rax, Operand(rbp, kIndexOffset));
  __ addq(rax, Immediate(Smi::FromInt(1)));
  __ movq(Operand(rbp, kIndexOffset), rax);

  __ bind(&entry);
  __ cmpq(rax, Operand(rbp, kLimitOffset));
  __ j(not_equal, &loop);

  // Invoke the function.
  ParameterCount actual(rax);
  __ SmiToInteger32(rax, rax);
  __ movq(rdi, Operand(rbp, kFunctionOffset));
  __ InvokeFunction(rdi, actual, CALL_FUNCTION);

  __ LeaveInternalFrame();
  __ ret(3 * kPointerSize);  // remove function, receiver, and arguments
}


// Load the built-in Array function from the current context.
static void GenerateLoadArrayFunction(MacroAssembler* masm, Register result) {
  // Load the global context.
  __ movq(result, Operand(rsi, Context::SlotOffset(Context::GLOBAL_INDEX)));
  __ movq(result, FieldOperand(result, GlobalObject::kGlobalContextOffset));
  // Load the Array function from the global context.
  __ movq(result,
          Operand(result, Context::SlotOffset(Context::ARRAY_FUNCTION_INDEX)));
}


// Number of empty elements to allocate for an empty array.
static const int kPreallocatedArrayElements = 4;


// Allocate an empty JSArray. The allocated array is put into the result
// register. If the parameter initial_capacity is larger than zero an elements
// backing store is allocated with this size and filled with the hole values.
// Otherwise the elements backing store is set to the empty FixedArray.
static void AllocateEmptyJSArray(MacroAssembler* masm,
                                 Register array_function,
                                 Register result,
                                 Register scratch1,
                                 Register scratch2,
                                 Register scratch3,
                                 int initial_capacity,
                                 Label* gc_required) {
  ASSERT(initial_capacity >= 0);

  // Load the initial map from the array function.
  __ movq(scratch1, FieldOperand(array_function,
                                 JSFunction::kPrototypeOrInitialMapOffset));

  // Allocate the JSArray object together with space for a fixed array with the
  // requested elements.
  int size = JSArray::kSize;
  if (initial_capacity > 0) {
    size += FixedArray::SizeFor(initial_capacity);
  }
  __ AllocateInNewSpace(size,
                        result,
                        scratch2,
                        scratch3,
                        gc_required,
                        TAG_OBJECT);

  // Allocated the JSArray. Now initialize the fields except for the elements
  // array.
  // result: JSObject
  // scratch1: initial map
  // scratch2: start of next object
  __ movq(FieldOperand(result, JSObject::kMapOffset), scratch1);
  __ Move(FieldOperand(result, JSArray::kPropertiesOffset),
          Factory::empty_fixed_array());
  // Field JSArray::kElementsOffset is initialized later.
  __ movq(FieldOperand(result, JSArray::kLengthOffset), Immediate(0));

  // If no storage is requested for the elements array just set the empty
  // fixed array.
  if (initial_capacity == 0) {
    __ Move(FieldOperand(result, JSArray::kElementsOffset),
            Factory::empty_fixed_array());
    return;
  }

  // Calculate the location of the elements array and set elements array member
  // of the JSArray.
  // result: JSObject
  // scratch2: start of next object
  __ lea(scratch1, Operand(result, JSArray::kSize));
  __ movq(FieldOperand(result, JSArray::kElementsOffset), scratch1);

  // Initialize the FixedArray and fill it with holes. FixedArray length is not
  // stored as a smi.
  // result: JSObject
  // scratch1: elements array
  // scratch2: start of next object
  __ Move(FieldOperand(scratch1, JSObject::kMapOffset),
          Factory::fixed_array_map());
  __ movq(FieldOperand(scratch1, Array::kLengthOffset),
          Immediate(initial_capacity));

  // Fill the FixedArray with the hole value. Inline the code if short.
  // Reconsider loop unfolding if kPreallocatedArrayElements gets changed.
  static const int kLoopUnfoldLimit = 4;
  ASSERT(kPreallocatedArrayElements <= kLoopUnfoldLimit);
  __ Move(scratch3, Factory::the_hole_value());
  if (initial_capacity <= kLoopUnfoldLimit) {
    // Use a scratch register here to have only one reloc info when unfolding
    // the loop.
    for (int i = 0; i < initial_capacity; i++) {
      __ movq(FieldOperand(scratch1,
                           FixedArray::kHeaderSize + i * kPointerSize),
              scratch3);
    }
  } else {
    Label loop, entry;
    __ jmp(&entry);
    __ bind(&loop);
    __ movq(Operand(scratch1, 0), scratch3);
    __ addq(scratch1, Immediate(kPointerSize));
    __ bind(&entry);
    __ cmpq(scratch1, scratch2);
    __ j(below, &loop);
  }
}


// Allocate a JSArray with the number of elements stored in a register. The
// register array_function holds the built-in Array function and the register
// array_size holds the size of the array as a smi. The allocated array is put
// into the result register and beginning and end of the FixedArray elements
// storage is put into registers elements_array and elements_array_end  (see
// below for when that is not the case). If the parameter fill_with_holes is
// true the allocated elements backing store is filled with the hole values
// otherwise it is left uninitialized. When the backing store is filled the
// register elements_array is scratched.
static void AllocateJSArray(MacroAssembler* masm,
                            Register array_function,  // Array function.
                            Register array_size,  // As a smi.
                            Register result,
                            Register elements_array,
                            Register elements_array_end,
                            Register scratch,
                            bool fill_with_hole,
                            Label* gc_required) {
  Label not_empty, allocated;

  // Load the initial map from the array function.
  __ movq(elements_array,
          FieldOperand(array_function,
                       JSFunction::kPrototypeOrInitialMapOffset));

  // Check whether an empty sized array is requested.
  __ testq(array_size, array_size);
  __ j(not_zero, &not_empty);

  // If an empty array is requested allocate a small elements array anyway. This
  // keeps the code below free of special casing for the empty array.
  int size = JSArray::kSize + FixedArray::SizeFor(kPreallocatedArrayElements);
  __ AllocateInNewSpace(size,
                        result,
                        elements_array_end,
                        scratch,
                        gc_required,
                        TAG_OBJECT);
  __ jmp(&allocated);

  // Allocate the JSArray object together with space for a FixedArray with the
  // requested elements.
  __ bind(&not_empty);
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ AllocateInNewSpace(JSArray::kSize + FixedArray::kHeaderSize,
                        times_half_pointer_size,  // array_size is a smi.
                        array_size,
                        result,
                        elements_array_end,
                        scratch,
                        gc_required,
                        TAG_OBJECT);

  // Allocated the JSArray. Now initialize the fields except for the elements
  // array.
  // result: JSObject
  // elements_array: initial map
  // elements_array_end: start of next object
  // array_size: size of array (smi)
  __ bind(&allocated);
  __ movq(FieldOperand(result, JSObject::kMapOffset), elements_array);
  __ Move(elements_array, Factory::empty_fixed_array());
  __ movq(FieldOperand(result, JSArray::kPropertiesOffset), elements_array);
  // Field JSArray::kElementsOffset is initialized later.
  __ movq(FieldOperand(result, JSArray::kLengthOffset), array_size);

  // Calculate the location of the elements array and set elements array member
  // of the JSArray.
  // result: JSObject
  // elements_array_end: start of next object
  // array_size: size of array (smi)
  __ lea(elements_array, Operand(result, JSArray::kSize));
  __ movq(FieldOperand(result, JSArray::kElementsOffset), elements_array);

  // Initialize the fixed array. FixedArray length is not stored as a smi.
  // result: JSObject
  // elements_array: elements array
  // elements_array_end: start of next object
  // array_size: size of array (smi)
  ASSERT(kSmiTag == 0);
  __ SmiToInteger64(array_size, array_size);
  __ Move(FieldOperand(elements_array, JSObject::kMapOffset),
          Factory::fixed_array_map());
  Label not_empty_2, fill_array;
  __ testq(array_size, array_size);
  __ j(not_zero, &not_empty_2);
  // Length of the FixedArray is the number of pre-allocated elements even
  // though the actual JSArray has length 0.
  __ movq(FieldOperand(elements_array, Array::kLengthOffset),
          Immediate(kPreallocatedArrayElements));
  __ jmp(&fill_array);
  __ bind(&not_empty_2);
  // For non-empty JSArrays the length of the FixedArray and the JSArray is the
  // same.
  __ movq(FieldOperand(elements_array, Array::kLengthOffset), array_size);

  // Fill the allocated FixedArray with the hole value if requested.
  // result: JSObject
  // elements_array: elements array
  // elements_array_end: start of next object
  __ bind(&fill_array);
  if (fill_with_hole) {
    Label loop, entry;
    __ Move(scratch, Factory::the_hole_value());
    __ lea(elements_array, Operand(elements_array,
                                   FixedArray::kHeaderSize - kHeapObjectTag));
    __ jmp(&entry);
    __ bind(&loop);
    __ movq(Operand(elements_array, 0), scratch);
    __ addq(elements_array, Immediate(kPointerSize));
    __ bind(&entry);
    __ cmpq(elements_array, elements_array_end);
    __ j(below, &loop);
  }
}


// Create a new array for the built-in Array function. This function allocates
// the JSArray object and the FixedArray elements array and initializes these.
// If the Array cannot be constructed in native code the runtime is called. This
// function assumes the following state:
//   rdi: constructor (built-in Array function)
//   rax: argc
//   rsp[0]: return address
//   rsp[8]: last argument
// This function is used for both construct and normal calls of Array. The only
// difference between handling a construct call and a normal call is that for a
// construct call the constructor function in rdi needs to be preserved for
// entering the generic code. In both cases argc in rax needs to be preserved.
// Both registers are preserved by this code so no need to differentiate between
// a construct call and a normal call.
static void ArrayNativeCode(MacroAssembler* masm,
                            Label *call_generic_code) {
  Label argc_one_or_more, argc_two_or_more;

  // Check for array construction with zero arguments.
  __ testq(rax, rax);
  __ j(not_zero, &argc_one_or_more);

  // Handle construction of an empty array.
  AllocateEmptyJSArray(masm,
                       rdi,
                       rbx,
                       rcx,
                       rdx,
                       r8,
                       kPreallocatedArrayElements,
                       call_generic_code);
  __ IncrementCounter(&Counters::array_function_native, 1);
  __ movq(rax, rbx);
  __ ret(kPointerSize);

  // Check for one argument. Bail out if argument is not smi or if it is
  // negative.
  __ bind(&argc_one_or_more);
  __ cmpq(rax, Immediate(1));
  __ j(not_equal, &argc_two_or_more);
  __ movq(rdx, Operand(rsp, kPointerSize));  // Get the argument from the stack.
  Condition not_positive_smi = __ CheckNotPositiveSmi(rdx);
  __ j(not_positive_smi, call_generic_code);

  // Handle construction of an empty array of a certain size. Bail out if size
  // is to large to actually allocate an elements array.
  __ JumpIfSmiGreaterEqualsConstant(rdx,
                                    JSObject::kInitialMaxFastElementArray,
                                    call_generic_code);

  // rax: argc
  // rdx: array_size (smi)
  // rdi: constructor
  // esp[0]: return address
  // esp[8]: argument
  AllocateJSArray(masm,
                  rdi,
                  rdx,
                  rbx,
                  rcx,
                  r8,
                  r9,
                  true,
                  call_generic_code);
  __ IncrementCounter(&Counters::array_function_native, 1);
  __ movq(rax, rbx);
  __ ret(2 * kPointerSize);

  // Handle construction of an array from a list of arguments.
  __ bind(&argc_two_or_more);
  __ movq(rdx, rax);
  __ Integer32ToSmi(rdx, rdx);  // Convet argc to a smi.
  // rax: argc
  // rdx: array_size (smi)
  // rdi: constructor
  // esp[0] : return address
  // esp[8] : last argument
  AllocateJSArray(masm,
                  rdi,
                  rdx,
                  rbx,
                  rcx,
                  r8,
                  r9,
                  false,
                  call_generic_code);
  __ IncrementCounter(&Counters::array_function_native, 1);

  // rax: argc
  // rbx: JSArray
  // rcx: elements_array
  // r8: elements_array_end (untagged)
  // esp[0]: return address
  // esp[8]: last argument

  // Location of the last argument
  __ lea(r9, Operand(rsp, kPointerSize));

  // Location of the first array element (Parameter fill_with_holes to
  // AllocateJSArrayis false, so the FixedArray is returned in rcx).
  __ lea(rdx, Operand(rcx, FixedArray::kHeaderSize - kHeapObjectTag));

  // rax: argc
  // rbx: JSArray
  // rdx: location of the first array element
  // r9: location of the last argument
  // esp[0]: return address
  // esp[8]: last argument
  Label loop, entry;
  __ movq(rcx, rax);
  __ jmp(&entry);
  __ bind(&loop);
  __ movq(kScratchRegister, Operand(r9, rcx, times_pointer_size, 0));
  __ movq(Operand(rdx, 0), kScratchRegister);
  __ addq(rdx, Immediate(kPointerSize));
  __ bind(&entry);
  __ decq(rcx);
  __ j(greater_equal, &loop);

  // Remove caller arguments from the stack and return.
  // rax: argc
  // rbx: JSArray
  // esp[0]: return address
  // esp[8]: last argument
  __ pop(rcx);
  __ lea(rsp, Operand(rsp, rax, times_pointer_size, 1 * kPointerSize));
  __ push(rcx);
  __ movq(rax, rbx);
  __ ret(0);
}


void Builtins::Generate_ArrayCode(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : argc
  //  -- rsp[0] : return address
  //  -- rsp[8] : last argument
  // -----------------------------------
  Label generic_array_code;

  // Get the Array function.
  GenerateLoadArrayFunction(masm, rdi);

  if (FLAG_debug_code) {
    // Initial map for the builtin Array function shoud be a map.
    __ movq(rbx, FieldOperand(rdi, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    ASSERT(kSmiTag == 0);
    Condition not_smi = __ CheckNotSmi(rbx);
    __ Assert(not_smi, "Unexpected initial map for Array function");
    __ CmpObjectType(rbx, MAP_TYPE, rcx);
    __ Assert(equal, "Unexpected initial map for Array function");
  }

  // Run the native code for the Array function called as a normal function.
  ArrayNativeCode(masm, &generic_array_code);

  // Jump to the generic array code in case the specialized code cannot handle
  // the construction.
  __ bind(&generic_array_code);
  Code* code = Builtins::builtin(Builtins::ArrayCodeGeneric);
  Handle<Code> array_code(code);
  __ Jump(array_code, RelocInfo::CODE_TARGET);
}


void Builtins::Generate_ArrayConstructCode(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax : argc
  //  -- rdi : constructor
  //  -- rsp[0] : return address
  //  -- rsp[8] : last argument
  // -----------------------------------
  Label generic_constructor;

  if (FLAG_debug_code) {
    // The array construct code is only set for the builtin Array function which
    // does always have a map.
    GenerateLoadArrayFunction(masm, rbx);
    __ cmpq(rdi, rbx);
    __ Assert(equal, "Unexpected Array function");
    // Initial map for the builtin Array function should be a map.
    __ movq(rbx, FieldOperand(rdi, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    ASSERT(kSmiTag == 0);
    Condition not_smi = __ CheckNotSmi(rbx);
    __ Assert(not_smi, "Unexpected initial map for Array function");
    __ CmpObjectType(rbx, MAP_TYPE, rcx);
    __ Assert(equal, "Unexpected initial map for Array function");
  }

  // Run the native code for the Array function called as constructor.
  ArrayNativeCode(masm, &generic_constructor);

  // Jump to the generic construct code in case the specialized code cannot
  // handle the construction.
  __ bind(&generic_constructor);
  Code* code = Builtins::builtin(Builtins::JSConstructStubGeneric);
  Handle<Code> generic_construct_stub(code);
  __ Jump(generic_construct_stub, RelocInfo::CODE_TARGET);
}


void Builtins::Generate_JSConstructCall(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- rax: number of arguments
  //  -- rdi: constructor function
  // -----------------------------------

  Label non_function_call;
  // Check that function is not a smi.
  __ JumpIfSmi(rdi, &non_function_call);
  // Check that function is a JSFunction.
  __ CmpObjectType(rdi, JS_FUNCTION_TYPE, rcx);
  __ j(not_equal, &non_function_call);

  // Jump to the function-specific construct stub.
  __ movq(rbx, FieldOperand(rdi, JSFunction::kSharedFunctionInfoOffset));
  __ movq(rbx, FieldOperand(rbx, SharedFunctionInfo::kConstructStubOffset));
  __ lea(rbx, FieldOperand(rbx, Code::kHeaderSize));
  __ jmp(rbx);

  // edi: called object
  // eax: number of arguments
  __ bind(&non_function_call);

  // Set expected number of arguments to zero (not changing eax).
  __ movq(rbx, Immediate(0));
  __ GetBuiltinEntry(rdx, Builtins::CALL_NON_FUNCTION_AS_CONSTRUCTOR);
  __ Jump(Handle<Code>(builtin(ArgumentsAdaptorTrampoline)),
          RelocInfo::CODE_TARGET);
}


void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
    // Enter a construct frame.
  __ EnterConstructFrame();

  // Store a smi-tagged arguments count on the stack.
  __ Integer32ToSmi(rax, rax);
  __ push(rax);

  // Push the function to invoke on the stack.
  __ push(rdi);

  // Try to allocate the object without transitioning into C code. If any of the
  // preconditions is not met, the code bails out to the runtime call.
  Label rt_call, allocated;
  if (FLAG_inline_new) {
    Label undo_allocation;

#ifdef ENABLE_DEBUGGER_SUPPORT
    ExternalReference debug_step_in_fp =
        ExternalReference::debug_step_in_fp_address();
    __ movq(kScratchRegister, debug_step_in_fp);
    __ cmpq(Operand(kScratchRegister, 0), Immediate(0));
    __ j(not_equal, &rt_call);
#endif

    // Verified that the constructor is a JSFunction.
    // Load the initial map and verify that it is in fact a map.
    // rdi: constructor
    __ movq(rax, FieldOperand(rdi, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi
    ASSERT(kSmiTag == 0);
    __ JumpIfSmi(rax, &rt_call);
    // rdi: constructor
    // rax: initial map (if proven valid below)
    __ CmpObjectType(rax, MAP_TYPE, rbx);
    __ j(not_equal, &rt_call);

    // Check that the constructor is not constructing a JSFunction (see comments
    // in Runtime_NewObject in runtime.cc). In which case the initial map's
    // instance type would be JS_FUNCTION_TYPE.
    // rdi: constructor
    // rax: initial map
    __ CmpInstanceType(rax, JS_FUNCTION_TYPE);
    __ j(equal, &rt_call);

    // Now allocate the JSObject on the heap.
    __ movzxbq(rdi, FieldOperand(rax, Map::kInstanceSizeOffset));
    __ shl(rdi, Immediate(kPointerSizeLog2));
    // rdi: size of new object
    __ AllocateInNewSpace(rdi,
                          rbx,
                          rdi,
                          no_reg,
                          &rt_call,
                          NO_ALLOCATION_FLAGS);
    // Allocated the JSObject, now initialize the fields.
    // rax: initial map
    // rbx: JSObject (not HeapObject tagged - the actual address).
    // rdi: start of next object
    __ movq(Operand(rbx, JSObject::kMapOffset), rax);
    __ LoadRoot(rcx, Heap::kEmptyFixedArrayRootIndex);
    __ movq(Operand(rbx, JSObject::kPropertiesOffset), rcx);
    __ movq(Operand(rbx, JSObject::kElementsOffset), rcx);
    // Set extra fields in the newly allocated object.
    // rax: initial map
    // rbx: JSObject
    // rdi: start of next object
    { Label loop, entry;
      __ LoadRoot(rdx, Heap::kUndefinedValueRootIndex);
      __ lea(rcx, Operand(rbx, JSObject::kHeaderSize));
      __ jmp(&entry);
      __ bind(&loop);
      __ movq(Operand(rcx, 0), rdx);
      __ addq(rcx, Immediate(kPointerSize));
      __ bind(&entry);
      __ cmpq(rcx, rdi);
      __ j(less, &loop);
    }

    // Add the object tag to make the JSObject real, so that we can continue and
    // jump into the continuation code at any time from now on. Any failures
    // need to undo the allocation, so that the heap is in a consistent state
    // and verifiable.
    // rax: initial map
    // rbx: JSObject
    // rdi: start of next object
    __ or_(rbx, Immediate(kHeapObjectTag));

    // Check if a non-empty properties array is needed.
    // Allocate and initialize a FixedArray if it is.
    // rax: initial map
    // rbx: JSObject
    // rdi: start of next object
    // Calculate total properties described map.
    __ movzxbq(rdx, FieldOperand(rax, Map::kUnusedPropertyFieldsOffset));
    __ movzxbq(rcx, FieldOperand(rax, Map::kPreAllocatedPropertyFieldsOffset));
    __ addq(rdx, rcx);
    // Calculate unused properties past the end of the in-object properties.
    __ movzxbq(rcx, FieldOperand(rax, Map::kInObjectPropertiesOffset));
    __ subq(rdx, rcx);
    // Done if no extra properties are to be allocated.
    __ j(zero, &allocated);
    __ Assert(positive, "Property allocation count failed.");

    // Scale the number of elements by pointer size and add the header for
    // FixedArrays to the start of the next object calculation from above.
    // rbx: JSObject
    // rdi: start of next object (will be start of FixedArray)
    // rdx: number of elements in properties array
    __ AllocateInNewSpace(FixedArray::kHeaderSize,
                          times_pointer_size,
                          rdx,
                          rdi,
                          rax,
                          no_reg,
                          &undo_allocation,
                          RESULT_CONTAINS_TOP);

    // Initialize the FixedArray.
    // rbx: JSObject
    // rdi: FixedArray
    // rdx: number of elements
    // rax: start of next object
    __ LoadRoot(rcx, Heap::kFixedArrayMapRootIndex);
    __ movq(Operand(rdi, JSObject::kMapOffset), rcx);  // setup the map
    __ movl(Operand(rdi, FixedArray::kLengthOffset), rdx);  // and length

    // Initialize the fields to undefined.
    // rbx: JSObject
    // rdi: FixedArray
    // rax: start of next object
    // rdx: number of elements
    { Label loop, entry;
      __ LoadRoot(rdx, Heap::kUndefinedValueRootIndex);
      __ lea(rcx, Operand(rdi, FixedArray::kHeaderSize));
      __ jmp(&entry);
      __ bind(&loop);
      __ movq(Operand(rcx, 0), rdx);
      __ addq(rcx, Immediate(kPointerSize));
      __ bind(&entry);
      __ cmpq(rcx, rax);
      __ j(below, &loop);
    }

    // Store the initialized FixedArray into the properties field of
    // the JSObject
    // rbx: JSObject
    // rdi: FixedArray
    __ or_(rdi, Immediate(kHeapObjectTag));  // add the heap tag
    __ movq(FieldOperand(rbx, JSObject::kPropertiesOffset), rdi);


    // Continue with JSObject being successfully allocated
    // rbx: JSObject
    __ jmp(&allocated);

    // Undo the setting of the new top so that the heap is verifiable. For
    // example, the map's unused properties potentially do not match the
    // allocated objects unused properties.
    // rbx: JSObject (previous new top)
    __ bind(&undo_allocation);
    __ UndoAllocationInNewSpace(rbx);
  }

  // Allocate the new receiver object using the runtime call.
  // rdi: function (constructor)
  __ bind(&rt_call);
  // Must restore rdi (constructor) before calling runtime.
  __ movq(rdi, Operand(rsp, 0));
  __ push(rdi);
  __ CallRuntime(Runtime::kNewObject, 1);
  __ movq(rbx, rax);  // store result in rbx

  // New object allocated.
  // rbx: newly allocated object
  __ bind(&allocated);
  // Retrieve the function from the stack.
  __ pop(rdi);

  // Retrieve smi-tagged arguments count from the stack.
  __ movq(rax, Operand(rsp, 0));
  __ SmiToInteger32(rax, rax);

  // Push the allocated receiver to the stack. We need two copies
  // because we may have to return the original one and the calling
  // conventions dictate that the called function pops the receiver.
  __ push(rbx);
  __ push(rbx);

  // Setup pointer to last argument.
  __ lea(rbx, Operand(rbp, StandardFrameConstants::kCallerSPOffset));

  // Copy arguments and receiver to the expression stack.
  Label loop, entry;
  __ movq(rcx, rax);
  __ jmp(&entry);
  __ bind(&loop);
  __ push(Operand(rbx, rcx, times_pointer_size, 0));
  __ bind(&entry);
  __ decq(rcx);
  __ j(greater_equal, &loop);

  // Call the function.
  ParameterCount actual(rax);
  __ InvokeFunction(rdi, actual, CALL_FUNCTION);

  // Restore context from the frame.
  __ movq(rsi, Operand(rbp, StandardFrameConstants::kContextOffset));

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, exit;
  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(rax, &use_receiver);

  // If the type of the result (stored in its map) is less than
  // FIRST_JS_OBJECT_TYPE, it is not an object in the ECMA sense.
  __ CmpObjectType(rax, FIRST_JS_OBJECT_TYPE, rcx);
  __ j(above_equal, &exit);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ movq(rax, Operand(rsp, 0));

  // Restore the arguments count and leave the construct frame.
  __ bind(&exit);
  __ movq(rbx, Operand(rsp, kPointerSize));  // get arguments count
  __ LeaveConstructFrame();

  // Remove caller arguments from the stack and return.
  ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  // TODO(smi): Find a way to abstract indexing by a smi.
  __ pop(rcx);
  // 1 * kPointerSize is offset of receiver.
  __ lea(rsp, Operand(rsp, rbx, times_half_pointer_size, 1 * kPointerSize));
  __ push(rcx);
  __ IncrementCounter(&Counters::constructed_objects, 1);
  __ ret(0);
}


static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  // Expects five C++ function parameters.
  // - Address entry (ignored)
  // - JSFunction* function (
  // - Object* receiver
  // - int argc
  // - Object*** argv
  // (see Handle::Invoke in execution.cc).

  // Platform specific argument handling. After this, the stack contains
  // an internal frame and the pushed function and receiver, and
  // register rax and rbx holds the argument count and argument array,
  // while rdi holds the function pointer and rsi the context.
#ifdef _WIN64
  // MSVC parameters in:
  // rcx : entry (ignored)
  // rdx : function
  // r8 : receiver
  // r9 : argc
  // [rsp+0x20] : argv

  // Clear the context before we push it when entering the JS frame.
  __ xor_(rsi, rsi);
  __ EnterInternalFrame();

  // Load the function context into rsi.
  __ movq(rsi, FieldOperand(rdx, JSFunction::kContextOffset));

  // Push the function and the receiver onto the stack.
  __ push(rdx);
  __ push(r8);

  // Load the number of arguments and setup pointer to the arguments.
  __ movq(rax, r9);
  // Load the previous frame pointer to access C argument on stack
  __ movq(kScratchRegister, Operand(rbp, 0));
  __ movq(rbx, Operand(kScratchRegister, EntryFrameConstants::kArgvOffset));
  // Load the function pointer into rdi.
  __ movq(rdi, rdx);
#else  // !defined(_WIN64)
  // GCC parameters in:
  // rdi : entry (ignored)
  // rsi : function
  // rdx : receiver
  // rcx : argc
  // r8  : argv

  __ movq(rdi, rsi);
  // rdi : function

  // Clear the context before we push it when entering the JS frame.
  __ xor_(rsi, rsi);
  // Enter an internal frame.
  __ EnterInternalFrame();

  // Push the function and receiver and setup the context.
  __ push(rdi);
  __ push(rdx);
  __ movq(rsi, FieldOperand(rdi, JSFunction::kContextOffset));

  // Load the number of arguments and setup pointer to the arguments.
  __ movq(rax, rcx);
  __ movq(rbx, r8);
#endif  // _WIN64

  // Set up the roots register.
  ExternalReference roots_address = ExternalReference::roots_address();
  __ movq(r13, roots_address);

  // Current stack contents:
  // [rsp + 2 * kPointerSize ... ]: Internal frame
  // [rsp + kPointerSize]         : function
  // [rsp]                        : receiver
  // Current register contents:
  // rax : argc
  // rbx : argv
  // rsi : context
  // rdi : function

  // Copy arguments to the stack in a loop.
  // Register rbx points to array of pointers to handle locations.
  // Push the values of these handles.
  Label loop, entry;
  __ xor_(rcx, rcx);  // Set loop variable to 0.
  __ jmp(&entry);
  __ bind(&loop);
  __ movq(kScratchRegister, Operand(rbx, rcx, times_pointer_size, 0));
  __ push(Operand(kScratchRegister, 0));  // dereference handle
  __ addq(rcx, Immediate(1));
  __ bind(&entry);
  __ cmpq(rcx, rax);
  __ j(not_equal, &loop);

  // Invoke the code.
  if (is_construct) {
    // Expects rdi to hold function pointer.
    __ Call(Handle<Code>(Builtins::builtin(Builtins::JSConstructCall)),
            RelocInfo::CODE_TARGET);
  } else {
    ParameterCount actual(rax);
    // Function must be in rdi.
    __ InvokeFunction(rdi, actual, CALL_FUNCTION);
  }

  // Exit the JS frame. Notice that this also removes the empty
  // context and the function left on the stack by the code
  // invocation.
  __ LeaveInternalFrame();
  // TODO(X64): Is argument correct? Is there a receiver to remove?
  __ ret(1 * kPointerSize);  // remove receiver
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

} }  // namespace v8::internal
