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

#ifndef V8_X64_MACRO_ASSEMBLER_X64_H_
#define V8_X64_MACRO_ASSEMBLER_X64_H_

#include "assembler.h"

namespace v8 {
namespace internal {

// Default scratch register used by MacroAssembler (and other code that needs
// a spare register). The register isn't callee save, and not used by the
// function calling convention.
static const Register kScratchRegister = r10;

// Forward declaration.
class JumpTarget;

struct SmiIndex {
  SmiIndex(Register index_register, ScaleFactor scale)
      : reg(index_register),
        scale(scale) {}
  Register reg;
  ScaleFactor scale;
};

// MacroAssembler implements a collection of frequently used macros.
class MacroAssembler: public Assembler {
 public:
  MacroAssembler(void* buffer, int size);

  void LoadRoot(Register destination, Heap::RootListIndex index);
  void CompareRoot(Register with, Heap::RootListIndex index);
  void CompareRoot(Operand with, Heap::RootListIndex index);
  void PushRoot(Heap::RootListIndex index);

  // ---------------------------------------------------------------------------
  // GC Support

  // Set the remembered set bit for [object+offset].
  // object is the object being stored into, value is the object being stored.
  // If offset is zero, then the scratch register contains the array index into
  // the elements array represented as a Smi.
  // All registers are clobbered by the operation.
  void RecordWrite(Register object,
                   int offset,
                   Register value,
                   Register scratch);

#ifdef ENABLE_DEBUGGER_SUPPORT
  // ---------------------------------------------------------------------------
  // Debugger Support

  void SaveRegistersToMemory(RegList regs);
  void RestoreRegistersFromMemory(RegList regs);
  void PushRegistersFromMemory(RegList regs);
  void PopRegistersToMemory(RegList regs);
  void CopyRegistersFromStackToMemory(Register base,
                                      Register scratch,
                                      RegList regs);
#endif

  // ---------------------------------------------------------------------------
  // Activation frames

  void EnterInternalFrame() { EnterFrame(StackFrame::INTERNAL); }
  void LeaveInternalFrame() { LeaveFrame(StackFrame::INTERNAL); }

  void EnterConstructFrame() { EnterFrame(StackFrame::CONSTRUCT); }
  void LeaveConstructFrame() { LeaveFrame(StackFrame::CONSTRUCT); }

  // Enter specific kind of exit frame; either EXIT or
  // EXIT_DEBUG. Expects the number of arguments in register rax and
  // sets up the number of arguments in register rdi and the pointer
  // to the first argument in register rsi.
  void EnterExitFrame(StackFrame::Type type, int result_size = 1);

  // Leave the current exit frame. Expects/provides the return value in
  // register rax:rdx (untouched) and the pointer to the first
  // argument in register rsi.
  void LeaveExitFrame(StackFrame::Type type, int result_size = 1);


  // ---------------------------------------------------------------------------
  // JavaScript invokes

  // Invoke the JavaScript function code by either calling or jumping.
  void InvokeCode(Register code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  InvokeFlag flag);

  void InvokeCode(Handle<Code> code,
                  const ParameterCount& expected,
                  const ParameterCount& actual,
                  RelocInfo::Mode rmode,
                  InvokeFlag flag);

  // Invoke the JavaScript function in the given register. Changes the
  // current context to the context in the function before invoking.
  void InvokeFunction(Register function,
                      const ParameterCount& actual,
                      InvokeFlag flag);

  // Invoke specified builtin JavaScript function. Adds an entry to
  // the unresolved list if the name does not resolve.
  void InvokeBuiltin(Builtins::JavaScript id, InvokeFlag flag);

  // Store the code object for the given builtin in the target register.
  void GetBuiltinEntry(Register target, Builtins::JavaScript id);


  // ---------------------------------------------------------------------------
  // Smi tagging, untagging and operations on tagged smis.

  // Conversions between tagged smi values and non-tagged integer values.

  // Tag an integer value. The result must be known to be a valid smi value.
  // Only uses the low 32 bits of the src register.
  void Integer32ToSmi(Register dst, Register src);

  // Tag an integer value if possible, or jump the integer value cannot be
  // represented as a smi. Only uses the low 32 bit of the src registers.
  void Integer32ToSmi(Register dst, Register src, Label* on_overflow);

  // Adds constant to src and tags the result as a smi.
  // Result must be a valid smi.
  void Integer64AddToSmi(Register dst, Register src, int constant);

  // Convert smi to 32-bit integer. I.e., not sign extended into
  // high 32 bits of destination.
  void SmiToInteger32(Register dst, Register src);

  // Convert smi to 64-bit integer (sign extended if necessary).
  void SmiToInteger64(Register dst, Register src);

  // Multiply a positive smi's integer value by a power of two.
  // Provides result as 64-bit integer value.
  void PositiveSmiTimesPowerOfTwoToInteger64(Register dst,
                                             Register src,
                                             int power);

  // Functions performing a check on a known or potential smi. Returns
  // a condition that is satisfied if the check is successful.

  // Is the value a tagged smi.
  Condition CheckSmi(Register src);

  // Is the value not a tagged smi.
  Condition CheckNotSmi(Register src);

  // Is the value a positive tagged smi.
  Condition CheckPositiveSmi(Register src);

  // Is the value not a positive tagged smi.
  Condition CheckNotPositiveSmi(Register src);

  // Are both values are tagged smis.
  Condition CheckBothSmi(Register first, Register second);

  // Is one of the values not a tagged smi.
  Condition CheckNotBothSmi(Register first, Register second);

  // Is the value the minimum smi value (since we are using
  // two's complement numbers, negating the value is known to yield
  // a non-smi value).
  Condition CheckIsMinSmi(Register src);

  // Check whether a tagged smi is equal to a constant.
  Condition CheckSmiEqualsConstant(Register src, int constant);

  // Check whether a tagged smi is greater than or equal to a constant.
  Condition CheckSmiGreaterEqualsConstant(Register src, int constant);

  // Checks whether an 32-bit integer value is a valid for conversion
  // to a smi.
  Condition CheckInteger32ValidSmiValue(Register src);

  // Test-and-jump functions. Typically combines a check function
  // above with a conditional jump.

  // Jump if the value cannot be represented by a smi.
  void JumpIfNotValidSmiValue(Register src, Label* on_invalid);

  // Jump to label if the value is a tagged smi.
  void JumpIfSmi(Register src, Label* on_smi);

  // Jump to label if the value is not a tagged smi.
  void JumpIfNotSmi(Register src, Label* on_not_smi);

  // Jump to label if the value is not a positive tagged smi.
  void JumpIfNotPositiveSmi(Register src, Label* on_not_smi);

  // Jump to label if the value is a tagged smi with value equal
  // to the constant.
  void JumpIfSmiEqualsConstant(Register src, int constant, Label* on_equals);

  // Jump to label if the value is a tagged smi with value greater than or equal
  // to the constant.
  void JumpIfSmiGreaterEqualsConstant(Register src,
                                      int constant,
                                      Label* on_equals);

  // Jump if either or both register are not smi values.
  void JumpIfNotBothSmi(Register src1, Register src2, Label* on_not_both_smi);

  // Operations on tagged smi values.

  // Smis represent a subset of integers. The subset is always equivalent to
  // a two's complement interpretation of a fixed number of bits.

  // Optimistically adds an integer constant to a supposed smi.
  // If the src is not a smi, or the result is not a smi, jump to
  // the label.
  void SmiTryAddConstant(Register dst,
                         Register src,
                         int32_t constant,
                         Label* on_not_smi_result);

  // Add an integer constant to a tagged smi, giving a tagged smi as result,
  // or jumping to a label if the result cannot be represented by a smi.
  // If the label is NULL, no testing on the result is done.
  void SmiAddConstant(Register dst,
                      Register src,
                      int32_t constant,
                      Label* on_not_smi_result);

  // Subtract an integer constant from a tagged smi, giving a tagged smi as
  // result, or jumping to a label if the result cannot be represented by a smi.
  // If the label is NULL, no testing on the result is done.
  void SmiSubConstant(Register dst,
                      Register src,
                      int32_t constant,
                      Label* on_not_smi_result);

  // Negating a smi can give a negative zero or too large positive value.
  void SmiNeg(Register dst,
              Register src,
              Label* on_not_smi_result);

  // Adds smi values and return the result as a smi.
  // If dst is src1, then src1 will be destroyed, even if
  // the operation is unsuccessful.
  void SmiAdd(Register dst,
              Register src1,
              Register src2,
              Label* on_not_smi_result);

  // Subtracts smi values and return the result as a smi.
  // If dst is src1, then src1 will be destroyed, even if
  // the operation is unsuccessful.
  void SmiSub(Register dst,
              Register src1,
              Register src2,
              Label* on_not_smi_result);

  // Multiplies smi values and return the result as a smi,
  // if possible.
  // If dst is src1, then src1 will be destroyed, even if
  // the operation is unsuccessful.
  void SmiMul(Register dst,
              Register src1,
              Register src2,
              Label* on_not_smi_result);

  // Divides one smi by another and returns the quotient.
  // Clobbers rax and rdx registers.
  void SmiDiv(Register dst,
              Register src1,
              Register src2,
              Label* on_not_smi_result);

  // Divides one smi by another and returns the remainder.
  // Clobbers rax and rdx registers.
  void SmiMod(Register dst,
              Register src1,
              Register src2,
              Label* on_not_smi_result);

  // Bitwise operations.
  void SmiNot(Register dst, Register src);
  void SmiAnd(Register dst, Register src1, Register src2);
  void SmiOr(Register dst, Register src1, Register src2);
  void SmiXor(Register dst, Register src1, Register src2);
  void SmiAndConstant(Register dst, Register src1, int constant);
  void SmiOrConstant(Register dst, Register src1, int constant);
  void SmiXorConstant(Register dst, Register src1, int constant);

  void SmiShiftLeftConstant(Register dst,
                            Register src,
                            int shift_value,
                            Label* on_not_smi_result);
  void SmiShiftLogicalRightConstant(Register dst,
                                  Register src,
                                  int shift_value,
                                  Label* on_not_smi_result);
  void SmiShiftArithmeticRightConstant(Register dst,
                                       Register src,
                                       int shift_value);

  // Shifts a smi value to the left, and returns the result if that is a smi.
  // Uses and clobbers rcx, so dst may not be rcx.
  void SmiShiftLeft(Register dst,
                    Register src1,
                    Register src2,
                    Label* on_not_smi_result);
  // Shifts a smi value to the right, shifting in zero bits at the top, and
  // returns the unsigned intepretation of the result if that is a smi.
  // Uses and clobbers rcx, so dst may not be rcx.
  void SmiShiftLogicalRight(Register dst,
                          Register src1,
                          Register src2,
                          Label* on_not_smi_result);
  // Shifts a smi value to the right, sign extending the top, and
  // returns the signed intepretation of the result. That will always
  // be a valid smi value, since it's numerically smaller than the
  // original.
  // Uses and clobbers rcx, so dst may not be rcx.
  void SmiShiftArithmeticRight(Register dst,
                               Register src1,
                               Register src2);

  // Specialized operations

  // Select the non-smi register of two registers where exactly one is a
  // smi. If neither are smis, jump to the failure label.
  void SelectNonSmi(Register dst,
                    Register src1,
                    Register src2,
                    Label* on_not_smis);

  // Converts, if necessary, a smi to a combination of number and
  // multiplier to be used as a scaled index.
  // The src register contains a *positive* smi value. The shift is the
  // power of two to multiply the index value by (e.g.
  // to index by smi-value * kPointerSize, pass the smi and kPointerSizeLog2).
  // The returned index register may be either src or dst, depending
  // on what is most efficient. If src and dst are different registers,
  // src is always unchanged.
  SmiIndex SmiToIndex(Register dst, Register src, int shift);

  // Converts a positive smi to a negative index.
  SmiIndex SmiToNegativeIndex(Register dst, Register src, int shift);

  // ---------------------------------------------------------------------------
  // Macro instructions

  // Expression support
  void Set(Register dst, int64_t x);
  void Set(const Operand& dst, int64_t x);

  // Handle support
  bool IsUnsafeSmi(Smi* value);
  bool IsUnsafeSmi(Handle<Object> value) {
    return IsUnsafeSmi(Smi::cast(*value));
  }

  void LoadUnsafeSmi(Register dst, Smi* source);
  void LoadUnsafeSmi(Register dst, Handle<Object> source) {
    LoadUnsafeSmi(dst, Smi::cast(*source));
  }

  void Move(Register dst, Handle<Object> source);
  void Move(const Operand& dst, Handle<Object> source);
  void Cmp(Register dst, Handle<Object> source);
  void Cmp(const Operand& dst, Handle<Object> source);
  void Push(Handle<Object> source);
  void Push(Smi* smi);

  // Control Flow
  void Jump(Address destination, RelocInfo::Mode rmode);
  void Jump(ExternalReference ext);
  void Jump(Handle<Code> code_object, RelocInfo::Mode rmode);

  void Call(Address destination, RelocInfo::Mode rmode);
  void Call(ExternalReference ext);
  void Call(Handle<Code> code_object, RelocInfo::Mode rmode);

  // Compare object type for heap object.
  // Always use unsigned comparisons: above and below, not less and greater.
  // Incoming register is heap_object and outgoing register is map.
  // They may be the same register, and may be kScratchRegister.
  void CmpObjectType(Register heap_object, InstanceType type, Register map);

  // Compare instance type for map.
  // Always use unsigned comparisons: above and below, not less and greater.
  void CmpInstanceType(Register map, InstanceType type);

  // FCmp is similar to integer cmp, but requires unsigned
  // jcc instructions (je, ja, jae, jb, jbe, je, and jz).
  void FCmp();

  // ---------------------------------------------------------------------------
  // Exception handling

  // Push a new try handler and link into try handler chain.  The return
  // address must be pushed before calling this helper.
  void PushTryHandler(CodeLocation try_location, HandlerType type);


  // ---------------------------------------------------------------------------
  // Inline caching support

  // Generates code that verifies that the maps of objects in the
  // prototype chain of object hasn't changed since the code was
  // generated and branches to the miss label if any map has. If
  // necessary the function also generates code for security check
  // in case of global object holders. The scratch and holder
  // registers are always clobbered, but the object register is only
  // clobbered if it the same as the holder register. The function
  // returns a register containing the holder - either object_reg or
  // holder_reg.
  Register CheckMaps(JSObject* object, Register object_reg,
                     JSObject* holder, Register holder_reg,
                     Register scratch, Label* miss);

  // Generate code for checking access rights - used for security checks
  // on access to global objects across environments. The holder register
  // is left untouched, but the scratch register and kScratchRegister,
  // which must be different, are clobbered.
  void CheckAccessGlobalProxy(Register holder_reg,
                              Register scratch,
                              Label* miss);


  // ---------------------------------------------------------------------------
  // Allocation support

  // Allocate an object in new space. If the new space is exhausted control
  // continues at the gc_required label. The allocated object is returned in
  // result and end of the new object is returned in result_end. The register
  // scratch can be passed as no_reg in which case an additional object
  // reference will be added to the reloc info. The returned pointers in result
  // and result_end have not yet been tagged as heap objects. If
  // result_contains_top_on_entry is true the content of result is known to be
  // the allocation top on entry (could be result_end from a previous call to
  // AllocateInNewSpace). If result_contains_top_on_entry is true scratch
  // should be no_reg as it is never used.
  void AllocateInNewSpace(int object_size,
                          Register result,
                          Register result_end,
                          Register scratch,
                          Label* gc_required,
                          AllocationFlags flags);

  void AllocateInNewSpace(int header_size,
                          ScaleFactor element_size,
                          Register element_count,
                          Register result,
                          Register result_end,
                          Register scratch,
                          Label* gc_required,
                          AllocationFlags flags);

  void AllocateInNewSpace(Register object_size,
                          Register result,
                          Register result_end,
                          Register scratch,
                          Label* gc_required,
                          AllocationFlags flags);

  // Undo allocation in new space. The object passed and objects allocated after
  // it will no longer be allocated. Make sure that no pointers are left to the
  // object(s) no longer allocated as they would be invalid when allocation is
  // un-done.
  void UndoAllocationInNewSpace(Register object);

  // ---------------------------------------------------------------------------
  // Support functions.

  // Check if result is zero and op is negative.
  void NegativeZeroTest(Register result, Register op, Label* then_label);

  // Check if result is zero and op is negative in code using jump targets.
  void NegativeZeroTest(CodeGenerator* cgen,
                        Register result,
                        Register op,
                        JumpTarget* then_target);

  // Check if result is zero and any of op1 and op2 are negative.
  // Register scratch is destroyed, and it must be different from op2.
  void NegativeZeroTest(Register result, Register op1, Register op2,
                        Register scratch, Label* then_label);

  // Try to get function prototype of a function and puts the value in
  // the result register. Checks that the function really is a
  // function and jumps to the miss label if the fast checks fail. The
  // function register will be untouched; the other register may be
  // clobbered.
  void TryGetFunctionPrototype(Register function,
                               Register result,
                               Label* miss);

  // Generates code for reporting that an illegal operation has
  // occurred.
  void IllegalOperation(int num_arguments);

  // ---------------------------------------------------------------------------
  // Runtime calls

  // Call a code stub.
  void CallStub(CodeStub* stub);

  // Return from a code stub after popping its arguments.
  void StubReturn(int argc);

  // Call a runtime routine.
  // Eventually this should be used for all C calls.
  void CallRuntime(Runtime::Function* f, int num_arguments);

  // Convenience function: Same as above, but takes the fid instead.
  void CallRuntime(Runtime::FunctionId id, int num_arguments);

  // Tail call of a runtime routine (jump).
  // Like JumpToRuntime, but also takes care of passing the number
  // of arguments.
  void TailCallRuntime(const ExternalReference& ext,
                       int num_arguments,
                       int result_size);

  // Jump to a runtime routine.
  void JumpToRuntime(const ExternalReference& ext, int result_size);


  // ---------------------------------------------------------------------------
  // Utilities

  void Ret();

  struct Unresolved {
    int pc;
    uint32_t flags;  // see Bootstrapper::FixupFlags decoders/encoders.
    const char* name;
  };
  List<Unresolved>* unresolved() { return &unresolved_; }

  Handle<Object> CodeObject() { return code_object_; }


  // ---------------------------------------------------------------------------
  // StatsCounter support

  void SetCounter(StatsCounter* counter, int value);
  void IncrementCounter(StatsCounter* counter, int value);
  void DecrementCounter(StatsCounter* counter, int value);


  // ---------------------------------------------------------------------------
  // Debugging

  // Calls Abort(msg) if the condition cc is not satisfied.
  // Use --debug_code to enable.
  void Assert(Condition cc, const char* msg);

  // Like Assert(), but always enabled.
  void Check(Condition cc, const char* msg);

  // Print a message to stdout and abort execution.
  void Abort(const char* msg);

  // Verify restrictions about code generated in stubs.
  void set_generating_stub(bool value) { generating_stub_ = value; }
  bool generating_stub() { return generating_stub_; }
  void set_allow_stub_calls(bool value) { allow_stub_calls_ = value; }
  bool allow_stub_calls() { return allow_stub_calls_; }

 private:
  List<Unresolved> unresolved_;
  bool generating_stub_;
  bool allow_stub_calls_;
  Handle<Object> code_object_;  // This handle will be patched with the code
                                // object on installation.

  // Helper functions for generating invokes.
  void InvokePrologue(const ParameterCount& expected,
                      const ParameterCount& actual,
                      Handle<Code> code_constant,
                      Register code_register,
                      Label* done,
                      InvokeFlag flag);

  // Prepares for a call or jump to a builtin by doing two things:
  // 1. Emits code that fetches the builtin's function object from the context
  //    at runtime, and puts it in the register rdi.
  // 2. Fetches the builtin's code object, and returns it in a handle, at
  //    compile time, so that later code can emit instructions to jump or call
  //    the builtin directly.  If the code object has not yet been created, it
  //    returns the builtin code object for IllegalFunction, and sets the
  //    output parameter "resolved" to false.  Code that uses the return value
  //    should then add the address and the builtin name to the list of fixups
  //    called unresolved_, which is fixed up by the bootstrapper.
  Handle<Code> ResolveBuiltin(Builtins::JavaScript id, bool* resolved);

  // Activation support.
  void EnterFrame(StackFrame::Type type);
  void LeaveFrame(StackFrame::Type type);

  // Allocation support helpers.
  void LoadAllocationTopHelper(Register result,
                               Register result_end,
                               Register scratch,
                               AllocationFlags flags);
  void UpdateAllocationTopHelper(Register result_end, Register scratch);
};


// The code patcher is used to patch (typically) small parts of code e.g. for
// debugging and other types of instrumentation. When using the code patcher
// the exact number of bytes specified must be emitted. Is not legal to emit
// relocation information. If any of these constraints are violated it causes
// an assertion.
class CodePatcher {
 public:
  CodePatcher(byte* address, int size);
  virtual ~CodePatcher();

  // Macro assembler to emit code.
  MacroAssembler* masm() { return &masm_; }

 private:
  byte* address_;  // The address of the code being patched.
  int size_;  // Number of bytes of the expected patch size.
  MacroAssembler masm_;  // Macro assembler used to generate the code.
};


// -----------------------------------------------------------------------------
// Static helper functions.

// Generate an Operand for loading a field from an object.
static inline Operand FieldOperand(Register object, int offset) {
  return Operand(object, offset - kHeapObjectTag);
}


// Generate an Operand for loading an indexed field from an object.
static inline Operand FieldOperand(Register object,
                                   Register index,
                                   ScaleFactor scale,
                                   int offset) {
  return Operand(object, index, scale, offset - kHeapObjectTag);
}


#ifdef GENERATED_CODE_COVERAGE
extern void LogGeneratedCodeCoverage(const char* file_line);
#define CODE_COVERAGE_STRINGIFY(x) #x
#define CODE_COVERAGE_TOSTRING(x) CODE_COVERAGE_STRINGIFY(x)
#define __FILE_LINE__ __FILE__ ":" CODE_COVERAGE_TOSTRING(__LINE__)
#define ACCESS_MASM(masm) {                                               \
    byte* x64_coverage_function =                                         \
        reinterpret_cast<byte*>(FUNCTION_ADDR(LogGeneratedCodeCoverage)); \
    masm->pushfd();                                                       \
    masm->pushad();                                                       \
    masm->push(Immediate(reinterpret_cast<int>(&__FILE_LINE__)));         \
    masm->call(x64_coverage_function, RelocInfo::RUNTIME_ENTRY);          \
    masm->pop(rax);                                                       \
    masm->popad();                                                        \
    masm->popfd();                                                        \
  }                                                                       \
  masm->
#else
#define ACCESS_MASM(masm) masm->
#endif


} }  // namespace v8::internal

#endif  // V8_X64_MACRO_ASSEMBLER_X64_H_
